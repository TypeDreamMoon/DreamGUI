// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

// Written from the module's Private root rather than as a bare name: this file sits one directory
// below the adapter, and only Private itself is on the include path.
#include "Driver/Designer/DreamDesignerDriver.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIAnchorData.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamWidgetBlueprint.h"

#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

/*
 * What a designer does when something is driven into it, one engine frame at a time.
 *
 * Every other designer test in this plugin reaches past the Slate host and calls the primitives:
 * CreateUnder on a palette operation, the tree-editing functions, the toolkit's own commands. That
 * proves the rules, and proves nothing about the ROUTE -- whether a drag that starts as a screen
 * position ever arrives at those rules, and whether the answers survive a frame boundary. These do
 * the second half, through FDreamDesignerDriver, and they have to be latent for it: a designer is
 * rebuilt, re-instanced and repainted by the engine loop, so the interesting states are the ones
 * that only exist on the NEXT frame.
 *
 * They all stand on one fact, which the first of them states: a headless editor never lays the
 * designer viewport out, so the driver has to hand it a size before any pixel means anything.
 */
namespace DreamDesignerDriverProbeLocal
{
	/** Everything one probe carries across the frames it spans. */
	struct FProbeState
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		TSharedPtr<DreamTests::FDreamDesignerDriver> Driver;
		/**
		 * Cleared by the first step that gives up.
		 *
		 * Latent commands do not stop when one of them fails -- the whole queue runs -- so every
		 * step asks this before touching anything, and the teardown at the end asks nothing at all.
		 */
		bool bAlive = true;
		/** Remembered by the step that made it, for the step that drops into it. */
		TWeakObjectPtr<UDreamWidget> PreviewOverlay;
	};

	/** Build the asset the New Widget Blueprint dialog builds, and open its designer. */
	TSharedRef<FProbeState> OpenProbe(const TCHAR* InName, bool bGiveRootAPanel)
	{
		TSharedRef<FProbeState> State = MakeShared<FProbeState>();
		State->Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
		State->Package->AddToRoot();
		State->Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UDreamUserWidget::StaticClass(), State->Package, FName(InName), BPTYPE_Normal,
			UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		if (State->Blueprint == nullptr)
		{
			State->bAlive = false;
			return State;
		}
		UDreamWidgetTree* Tree = State->Blueprint->GetOrCreateWidgetTree();
		Tree->RootWidget->SetDisplayName(TEXT("Root"));
		if (bGiveRootAPanel)
		{
			Tree->RootWidget->CreateNewLayoutContainer(UDreamLayoutContainerCanvasPanel::StaticClass());
		}
		FKismetEditorUtilities::CompileBlueprint(State->Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		State->Driver = DreamTests::FDreamDesignerDriver::Open(State->Blueprint);
		if (!State->Driver.IsValid())
		{
			State->bAlive = false;
		}
		return State;
	}

	/**
	 * Give the designer a size to be driven against.
	 *
	 * The tests below are about what a drop DOES, not about whether a headless editor hands the
	 * viewport an extent; that second question has a probe of its own above, and it has to keep
	 * reporting what it finds rather than what it could arrange. So everything else arranges one
	 * and gets on with its own claim. On a designer that already has a size this changes nothing.
	 */
	void ArrangeViewportSize(const TSharedRef<FProbeState>& InState)
	{
		if (InState->Driver.IsValid())
		{
			InState->Driver->EnsureHeadlessSize(FIntPoint(1280, 720));
		}
	}

	void EnqueueStep(TFunction<bool()> InStep)
	{
		// Through a named local rather than straight into the macro: a lambda's capture list carries
		// commas, and the preprocessor would cut the macro argument at the first one.
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	/** Let InFrames engine frames pass. One latent Update is one frame. */
	void EnqueueFrames(int32 InFrames)
	{
		TSharedRef<int32> Remaining = MakeShared<int32>(FMath::Max(InFrames, 0));
		TFunction<bool()> Step = [Remaining]()
		{
			if (*Remaining <= 0)
			{
				return true;
			}
			--(*Remaining);
			return *Remaining <= 0;
		};
		EnqueueStep(Step);
	}

	/**
	 * Close the designer and let the temporary asset go, whatever happened before.
	 *
	 * Two steps, a frame apart, for the same reason the older designer fixtures tick Slate between
	 * them: the close is deferred and a toolkit that is still alive still ticks, so letting the
	 * package leave the root set in the same breath puts a preview rebuild on a half-collected asset.
	 */
	void EnqueueTeardown(TSharedRef<FProbeState> InState)
	{
		TFunction<bool()> CloseStep = [InState]()
		{
			if (InState->Driver.IsValid())
			{
				InState->Driver->Close();
				InState->Driver.Reset();
			}
			return true;
		};
		EnqueueStep(CloseStep);

		TFunction<bool()> ReleaseStep = [InState]()
		{
			if (InState->Package != nullptr)
			{
				InState->Package->RemoveFromRoot();
				InState->Package = nullptr;
			}
			InState->Blueprint = nullptr;
			return true;
		};
		EnqueueStep(ReleaseStep);
	}

	/** The authoring tree's root -- the asset, which is what an edit has to reach to be an edit. */
	UDreamWidget* TemplateRoot(const TSharedRef<FProbeState>& InState)
	{
		return ::IsValid(InState->Blueprint) && ::IsValid(InState->Blueprint->WidgetTree)
			? InState->Blueprint->WidgetTree->RootWidget.Get()
			: nullptr;
	}

	UDreamWidget* FirstLiveChild(const UDreamWidget* InParent)
	{
		if (!::IsValid(InParent))
		{
			return nullptr;
		}
		for (UDreamWidget* Child : InParent->GetChildren())
		{
			if (::IsValid(Child))
			{
				return Child;
			}
		}
		return nullptr;
	}

	/**
	 * Whether a widget is stretched over the whole of its parent, measured the way the placement
	 * rule writes it: a zero DELTA, which means "exactly the parent", rather than a size that would
	 * have resolved against whatever the parent happened to measure at the time.
	 */
	bool FillsItsParent(const UDreamWidget* InWidget)
	{
		if (!::IsValid(InWidget))
		{
			return false;
		}
		const FDreamUIAnchorData Anchors = InWidget->GetAnchorData();
		return Anchors.AnchorMin.Equals(FVector2D::ZeroVector) && Anchors.AnchorMax.Equals(FVector2D(1.0, 1.0))
			&& Anchors.AnchoredPosition.IsNearlyZero() && Anchors.SizeDelta.IsNearlyZero();
	}

	/** A hole anywhere under InRoot. Re-instancing has put one here before, and it crashed next frame. */
	bool HasNullDescendant(const UDreamWidget* InRoot)
	{
		if (!::IsValid(InRoot))
		{
			return false;
		}
		for (const UDreamWidget* Child : InRoot->GetChildren())
		{
			if (!::IsValid(Child) || HasNullDescendant(Child))
			{
				return true;
			}
		}
		return false;
	}
}

/*
 * A headless designer has no size of its own, and takes one when it is offered.
 *
 * Nothing lays the viewport out when there is no window on screen, so FSceneViewport keeps the zero
 * size it was constructed with, and everything a driver does follows that zero down: PixelToScreen
 * divides by the viewport size and answers the origin for every pixel, and FSceneViewport's own
 * "GetSizeXY() != ZeroValue" guard drops every pointer event before the viewport client sees it, so
 * a drag would arrive nowhere and a click would not arrive at all. SetFixedViewportSize fills that
 * hole from the outside, which is the only reason the three tests after this one can exist.
 *
 * The size BEFORE the offer is recorded rather than asserted, because it is a property of the
 * environment and not of this plugin: zero under a headless run, something else under a real window,
 * and a test that demanded either would be red in the other. What is asserted is the part that
 * belongs to us -- that the offer is taken, exactly, and that the conversions still invert over the
 * size that was taken.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDriverViewportSizeTest,
	"DreamGUI.Designer.Driver.AHeadlessDesignerViewportTakesTheSizeItIsGiven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerDriverViewportSizeTest::RunTest(const FString&)
{
	using namespace DreamDesignerDriverProbeLocal;

	TSharedRef<FProbeState> State = OpenProbe(TEXT("DesignerDriverViewportSize"), /*bGiveRootAPanel*/true);
	TestTrue(TEXT("The designer opened and handed over its viewport"), State->Driver.IsValid());

	// Two frames, because one is how long it takes the toolkit to exist and the second is the first
	// one Slate could have laid the new tab out in.
	EnqueueFrames(2);

	TFunction<bool()> Step = [this, State]()
	{
		if (!State->Driver.IsValid())
		{
			return true;
		}
		const FIntPoint Found = State->Driver->ViewportPixelSize();
		AddInfo(FString::Printf(TEXT("Two frames after opening, the designer viewport measured %dx%d."),
			Found.X, Found.Y));

		// Exact, not merely non-zero: a size that arrives as something other than what was asked for
		// would put every pixel these tests name somewhere else, quietly.
		const FIntPoint Asked(1280, 720);
		TestTrue(TEXT("The viewport takes the size it is given"), State->Driver->EnsureHeadlessSize(Asked));
		TestEqual(TEXT("and measures exactly that afterwards"), State->Driver->ViewportPixelSize(), Asked);

		// Said here because this is where the size comes from: the arranged extent is what every
		// pixel below is divided by, so a round trip that survives it survives the synthetic
		// geometry that comes with it.
		const FIntPoint Centre = State->Driver->ViewportCentrePixel();
		TestEqual(TEXT("and a pixel survives the trip out to screen space and back"),
			State->Driver->ScreenToPixel(State->Driver->PixelToScreen(Centre)), Centre);
		return true;
	};
	EnqueueStep(Step);

	EnqueueTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDriverDropFillsEmptyRootTest,
	"DreamGUI.Designer.Driver.DroppingAButtonIntoAnEmptyRootFillsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerDriverDropFillsEmptyRootTest::RunTest(const FString&)
{
	using namespace DreamDesignerDriverProbeLocal;

	// A root that arranges nothing, which is what the New Widget Blueprint dialog builds when its
	// root-panel picker is answered with None, and the state the first-drop rule is written for.
	TSharedRef<FProbeState> State = OpenProbe(TEXT("DesignerDriverDropFills"), /*bGiveRootAPanel*/false);
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	ArrangeViewportSize(State);

	EnqueueFrames(2);

	TFunction<bool()> Drop = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const FIntPoint Size = State->Driver->ViewportPixelSize();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			AddError(TEXT("The designer viewport is still zero-sized after a size was asked for, so ")
				TEXT("there is no pixel to drop onto and nothing below can be read. What gives the ")
				TEXT("viewport its extent has to be fixed first; this test has no opinion until then."));
			State->bAlive = false;
			return true;
		}
		TestTrue(TEXT("The drag from the palette is accepted by the design surface"),
			State->Driver->DropFromPalette(UDreamButton::StaticClass(), State->Driver->ViewportCentrePixel()));
		return true;
	};
	EnqueueStep(Drop);

	EnqueueFrames(1);

	TFunction<bool()> Check = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		UDreamWidget* PreviewParent = State->Driver->BlueprintRoot();
		UDreamWidget* Wrapper = State->Driver->PreviewRoot();
		if (TestNotNull(TEXT("There is still a preview root to look at"), PreviewParent))
		{
			TestEqual(TEXT("Exactly one widget landed under the Blueprint's root"),
				State->Driver->ChildCountUnder(PreviewParent), 1);
			UDreamWidget* Dropped = FirstLiveChild(PreviewParent);
			if (TestNotNull(TEXT("and it is reachable as a child"), Dropped))
			{
				// The wrapper bug's exact shape: a drop resolved its container by walking up past
				// the Blueprint root into the designer's own Overlay, so the new widget filled the
				// screen and vanished from the hierarchy at the same time.
				TestTrue(TEXT("Its parent is the Blueprint's root, not the designer's wrapper above it"),
					Dropped->GetParent() == PreviewParent);
				TestNotNull(TEXT("There IS a wrapper above the Blueprint root"), Wrapper);
				TestTrue(TEXT("and it is a different widget, so 'not the wrapper' means something"),
					Wrapper != PreviewParent);
			}
		}

		// The asset is the half that survives the next rebuild, so it is the half the rules are
		// asserted against.
		UDreamWidget* AuthoredRoot = TemplateRoot(State);
		if (TestNotNull(TEXT("The authoring tree still has a root"), AuthoredRoot))
		{
			TestEqual(TEXT("One widget reached the asset too"),
				State->Driver->ChildCountUnder(AuthoredRoot), 1);
			UDreamWidget* AuthoredChild = FirstLiveChild(AuthoredRoot);
			if (TestNotNull(TEXT("and it is there to be read"), AuthoredChild))
			{
				TestTrue(TEXT("It is a Button, which is the palette row that was dragged"),
					AuthoredChild->IsA(UDreamButton::StaticClass()));
				TestTrue(TEXT("and it fills the root, with nothing left over from where the cursor let go"),
					FillsItsParent(AuthoredChild));
			}
		}
		return true;
	};
	EnqueueStep(Check);

	EnqueueTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDriverDropIntoOverlayTest,
	"DreamGUI.Designer.Driver.DroppingIntoAnOverlayAlignsTopLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerDriverDropIntoOverlayTest::RunTest(const FString&)
{
	using namespace DreamDesignerDriverProbeLocal;

	TSharedRef<FProbeState> State = OpenProbe(TEXT("DesignerDriverDropIntoOverlay"), /*bGiveRootAPanel*/false);
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	ArrangeViewportSize(State);

	EnqueueFrames(2);

	TFunction<bool()> DropOverlay = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const FIntPoint Size = State->Driver->ViewportPixelSize();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			AddError(TEXT("The designer viewport is still zero-sized after a size was asked for, so ")
				TEXT("there is no pixel to drop onto and nothing below can be read. What gives the ")
				TEXT("viewport its extent has to be fixed first; this test has no opinion until then."));
			State->bAlive = false;
			return true;
		}
		TestTrue(TEXT("An Overlay is dropped on the empty root"),
			State->Driver->DropFromPalette(UDreamLayoutContainerOverlay::StaticClass(), State->Driver->ViewportCentrePixel()));
		return true;
	};
	EnqueueStep(DropOverlay);

	EnqueueFrames(1);

	TFunction<bool()> DropButton = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		UDreamWidget* Overlay = FirstLiveChild(State->Driver->BlueprintRoot());
		if (!TestNotNull(TEXT("The Overlay is in the preview, where it can be pointed at"), Overlay))
		{
			State->bAlive = false;
			return true;
		}
		State->PreviewOverlay = Overlay;
		// A pixel the Overlay actually occupies, asked of the same projection the designer draws its
		// own outlines with -- not the viewport centre, which only happens to be inside it.
		const TOptional<FIntPoint> Pixel = State->Driver->WidgetPixel(Overlay);
		if (!TestTrue(TEXT("The Overlay projects to a pixel"), Pixel.IsSet()))
		{
			State->bAlive = false;
			return true;
		}
		TestTrue(TEXT("A Button is dropped inside the Overlay"),
			State->Driver->DropFromPalette(UDreamButton::StaticClass(), Pixel.GetValue()));
		return true;
	};
	EnqueueStep(DropButton);

	EnqueueFrames(1);

	TFunction<bool()> Check = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		UDreamWidget* AuthoredOverlay = FirstLiveChild(TemplateRoot(State));
		if (!TestNotNull(TEXT("The Overlay reached the asset"), AuthoredOverlay))
		{
			return true;
		}
		TestEqual(TEXT("The Button is the Overlay's one child, not the root's second"),
			State->Driver->ChildCountUnder(AuthoredOverlay), 1);
		UDreamWidget* AuthoredButton = FirstLiveChild(AuthoredOverlay);
		if (!TestNotNull(TEXT("and it is there to be read"), AuthoredButton))
		{
			return true;
		}
		TestTrue(TEXT("It is a Button"), AuthoredButton->IsA(UDreamButton::StaticClass()));
		TestTrue(TEXT("and its parent is the Overlay"), AuthoredButton->GetParent() == AuthoredOverlay);

		// A slot that says nothing means Fill, which is what a button dropped on an overlay used to
		// get -- and it swallowed the overlay. The drop writes the panel's own answer instead.
		UDreamPanelSlot* Slot = AuthoredButton->GetPanelSlot();
		if (TestNotNull(TEXT("The Button has a slot in its panel"), Slot))
		{
			TestTrue(TEXT("Its slot starts in the Overlay's top-left corner, the way UOverlaySlot does"),
				Slot->HorizontalAlignment == EDreamPanelHorizontalAlignment::Left
				&& Slot->VerticalAlignment == EDreamPanelVerticalAlignment::Top);
		}

		// The preview says the same thing, which is the half an author sees.
		if (UDreamWidget* PreviewOverlay = State->PreviewOverlay.Get())
		{
			TestEqual(TEXT("The preview agrees the Button is inside the Overlay"),
				State->Driver->ChildCountUnder(PreviewOverlay), 1);
		}
		return true;
	};
	EnqueueStep(Check);

	EnqueueTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDriverCompileSurvivesTest,
	"DreamGUI.Designer.Driver.CompilingWithTheDesignerOpenSurvivesTheNextFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerDriverCompileSurvivesTest::RunTest(const FString&)
{
	using namespace DreamDesignerDriverProbeLocal;

	TSharedRef<FProbeState> State = OpenProbe(TEXT("DesignerDriverCompileSurvives"), /*bGiveRootAPanel*/false);
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	ArrangeViewportSize(State);

	EnqueueFrames(2);

	TFunction<bool()> DropThenCompile = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const FIntPoint Size = State->Driver->ViewportPixelSize();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			AddError(TEXT("The designer viewport is still zero-sized after a size was asked for, so ")
				TEXT("there is no pixel to drop onto and nothing below can be read. What gives the ")
				TEXT("viewport its extent has to be fixed first; this test has no opinion until then."));
			State->bAlive = false;
			return true;
		}
		TestTrue(TEXT("A Button is dropped"),
			State->Driver->DropFromPalette(UDreamButton::StaticClass(), State->Driver->ViewportCentrePixel()));
		// Through the toolkit's own compile, because what is under test is the aftermath: the class
		// is rebuilt, every live instance is re-instanced onto a copy, and the preview the designer
		// is still drawing is one of them.
		State->Driver->Compile();
		return true;
	};
	EnqueueStep(DropThenCompile);

	// Three, because the crash this is written against did not happen during the compile. It
	// happened on the frame after it, walking the children of a re-instanced copy whose shared
	// sub-objects had already been collected.
	EnqueueFrames(3);

	TFunction<bool()> Check = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		TestTrue(TEXT("The toolkit is still the one editing this asset"), State->Driver->IsToolkitOpen());
		UDreamWidget* PreviewParent = State->Driver->BlueprintRoot();
		if (TestNotNull(TEXT("There is still a preview root three frames after the compile"), PreviewParent))
		{
			TestFalse(TEXT("and nothing under it is a hole"), HasNullDescendant(PreviewParent));
			TestEqual(TEXT("The dropped widget is still there"),
				State->Driver->ChildCountUnder(PreviewParent), 1);
		}
		return true;
	};
	EnqueueStep(Check);

	EnqueueTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDriverUndoRedoDropTest,
	"DreamGUI.Designer.Driver.UndoRemovesTheDroppedWidgetAndRedoBringsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerDriverUndoRedoDropTest::RunTest(const FString&)
{
	using namespace DreamDesignerDriverProbeLocal;

	TSharedRef<FProbeState> State = OpenProbe(TEXT("DesignerDriverUndoRedo"), /*bGiveRootAPanel*/false);
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	ArrangeViewportSize(State);

	EnqueueFrames(2);

	TFunction<bool()> Drop = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const FIntPoint Size = State->Driver->ViewportPixelSize();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			AddError(TEXT("The designer viewport is still zero-sized after a size was asked for, so ")
				TEXT("there is no pixel to drop onto and nothing below can be read. What gives the ")
				TEXT("viewport its extent has to be fixed first; this test has no opinion until then."));
			State->bAlive = false;
			return true;
		}
		TestTrue(TEXT("A Button is dropped"),
			State->Driver->DropFromPalette(UDreamButton::StaticClass(), State->Driver->ViewportCentrePixel()));
		return true;
	};
	EnqueueStep(Drop);

	EnqueueFrames(1);

	TFunction<bool()> Undo = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		TestEqual(TEXT("The asset holds the dropped widget before the undo"),
			State->Driver->ChildCountUnder(TemplateRoot(State)), 1);
		// A drop used to leave nothing on the undo stack at all, so Ctrl+Z reached past it into
		// whatever the author had done before.
		State->Driver->Undo();
		return true;
	};
	EnqueueStep(Undo);

	EnqueueFrames(1);

	TFunction<bool()> Redo = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		TestEqual(TEXT("Undo took the dropped widget back out of the asset"),
			State->Driver->ChildCountUnder(TemplateRoot(State)), 0);
		State->Driver->Redo();
		return true;
	};
	EnqueueStep(Redo);

	EnqueueFrames(1);

	TFunction<bool()> Check = [this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		TestEqual(TEXT("and Redo put it back"),
			State->Driver->ChildCountUnder(TemplateRoot(State)), 1);
		return true;
	};
	EnqueueStep(Check);

	EnqueueTeardown(State);
	return true;
}

#endif
