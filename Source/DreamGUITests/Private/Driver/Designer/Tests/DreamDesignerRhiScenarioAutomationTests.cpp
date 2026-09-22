// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetDesignerModes.h"
#include "DreamWidgetBlueprint.h"

#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"

/*
 * The designer, rendering, while an author does the things authors do to it.
 *
 * The crashes this file hunts do not exist under -nullrhi. The designer draws its preview through a
 * render path of its own -- render graph passes recorded in parallel, finished by passes that run
 * immediately -- and the one that reached a user, a null view uniform buffer while a button was being
 * dragged in, was two of those passes disagreeing about how long a view lived. That only happens when
 * something is really drawn, so every test here carries NonNullRHI and the headless suite skips them.
 *
 * Each scenario is an ordinary editing session squeezed into as few frames as it will go: every step
 * is followed by frames in which the designer viewport is drawn, so a change on the game thread keeps
 * meeting a render thread that is still working on the frame before it. Nothing is flushed between
 * steps, deliberately -- flushing would serialise exactly the overlap being looked for. The assertions
 * are the plain ones -- the toolkit is still open, the preview still has a root and no holes, the
 * asset holds what was put in it -- because the finding these are after is a crash, and the suite's
 * fatal count is what reports one.
 *
 * The render-graph switches are logged, never changed. Whether a crash reproduces depends on them, and
 * a scenario that ran with settings the author's editor does not use would be hunting somebody else's
 * crash; both come from the same ini.
 */
namespace DreamDesignerRhiScenarioLocal
{
	/** The size a headless designer is given. The viewport keeps a real window's size when it has one. */
	const FIntPoint HeadlessViewportSize(1280, 720);
	/** Fixed, and logged, so a crash that needed one particular layout of buttons can be had again. */
	constexpr int32 ScenarioSeed = 20260922;
	/** Each scenario draws at least this many frames, or it has not tested rendering at all. */
	constexpr int32 MinimumFramesDrawn = 10;

	/** One designer session, held across the frames of one scenario. */
	struct FScenario
	{
		DreamTests::FDesignerTestAsset Asset;
		TSharedPtr<DreamTests::FDreamDesignerDriver> Driver;
		/**
		 * Cleared by the first step that cannot go on. Latent commands do not stop when one fails, so
		 * every step asks this before touching anything; the teardown asks nothing.
		 */
		bool bAlive = true;
		int32 FramesDrawn = 0;
		int32 DropsRefused = 0;
		int32 OpensSucceeded = 0;
		FRandomStream Random;
		/** Set only by the scenario that changes the DPI preview, so the teardown can put it back. */
		TOptional<bool> DpiPreviewBefore;
	};
	using FScenarioRef = TSharedRef<FScenario>;

	/**
	 * Queue one step. Through a named local rather than straight into the macro: a lambda's capture
	 * list carries commas, and the preprocessor would cut the macro argument at the first one.
	 */
	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	/** What the render graph was told to do in parallel, so a crash report carries it. */
	void LogRenderGraphSettings(FAutomationTestBase& InTest)
	{
		for (const TCHAR* Name : { TEXT("r.RDG.ParallelExecute"), TEXT("r.RDG.ParallelSetup") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				InTest.AddInfo(FString::Printf(TEXT("%s = %s"), Name, *Variable->GetString()));
			}
			else
			{
				InTest.AddInfo(FString::Printf(TEXT("%s does not exist in this build."), Name));
			}
		}
		InTest.AddInfo(FString::Printf(TEXT("Scenario seed %d."), ScenarioSeed));
	}

	bool OpenDesigner(FAutomationTestBase& InTest, const FScenarioRef& InScenario)
	{
		InScenario->Driver = DreamTests::FDreamDesignerDriver::Open(InScenario->Asset.Blueprint);
		if (!InScenario->Driver.IsValid())
		{
			InTest.AddError(TEXT("The designer did not open, or opened without a viewport (see LogDreamDesignerDriver)."));
			return false;
		}
		if (!InScenario->Driver->EnsureHeadlessSize(HeadlessViewportSize))
		{
			InTest.AddError(FString::Printf(TEXT("The designer viewport could not be given a size; it measures %dx%d."),
				InScenario->Driver->ViewportPixelSize().X, InScenario->Driver->ViewportPixelSize().Y));
			return false;
		}
		++InScenario->OpensSucceeded;
		return true;
	}

	/** A fresh asset with a Canvas Panel on its root, so every button dropped on it is placed where it fell. */
	FScenarioRef BeginScenario(FAutomationTestBase& InTest, const TCHAR* InName, bool bOpen = true)
	{
		FScenarioRef Scenario = MakeShared<FScenario>();
		Scenario->Random.Initialize(ScenarioSeed);
		Scenario->Asset = DreamTests::CreateDesignerTestAsset(InName, /*bGiveRootAPanel*/true);
		if (!Scenario->Asset.IsValid())
		{
			InTest.AddError(FString::Printf(TEXT("The Widget Blueprint for %s could not be created."), InName));
			Scenario->bAlive = false;
			return Scenario;
		}
		if (bOpen && !OpenDesigner(InTest, Scenario))
		{
			Scenario->bAlive = false;
		}
		return Scenario;
	}

	/** Run InAction once, in the next frame, if the scenario is still standing. */
	void EnqueueAction(const FScenarioRef& InScenario, TFunction<void()> InAction)
	{
		TFunction<bool()> Step = [InScenario, InAction]()
		{
			if (InScenario->bAlive)
			{
				InAction();
			}
			return true;
		};
		EnqueueStep(Step);
	}

	/**
	 * Let InFrames frames pass, drawing every listed designer once in each.
	 *
	 * The count reaches zero on one Update and the step reports done on the next, for the reason the
	 * pixel probes give: a latent command that finishes lets the following one run in the SAME tick,
	 * so finishing on the last draw would put the next edit into the frame that draw belongs to. The
	 * engine's own loop draws a realtime designer too whenever it draws at all; this is the draw that
	 * does not depend on that.
	 */
	void EnqueueDrawnFrames(const TArray<FScenarioRef>& InScenarios, int32 InFrames)
	{
		TSharedRef<int32> Remaining = MakeShared<int32>(FMath::Max(InFrames, 0));
		TArray<FScenarioRef> Scenarios = InScenarios;
		TFunction<bool()> Step = [Scenarios, Remaining]()
		{
			if (*Remaining <= 0)
			{
				return true;
			}
			for (const FScenarioRef& Scenario : Scenarios)
			{
				if (Scenario->bAlive && Scenario->Driver.IsValid() && Scenario->Driver->DrawFrame())
				{
					++Scenario->FramesDrawn;
				}
			}
			--(*Remaining);
			return false;
		};
		EnqueueStep(Step);
	}

	/** Queue an assertion to run once, a frame after whatever came before it. */
	void EnqueueCheck(TFunction<void()> InCheck)
	{
		TFunction<bool()> Step = [InCheck]()
		{
			InCheck();
			return true;
		};
		EnqueueStep(Step);
	}

	/**
	 * Close every designer and let every asset go, whatever the steps before did.
	 *
	 * The assets a frame after the closes, because the close is deferred and a toolkit that is still
	 * alive still ticks: releasing first would put a preview rebuild on a half-collected asset.
	 */
	void EnqueueTeardown(const TArray<FScenarioRef>& InScenarios)
	{
		TArray<FScenarioRef> Scenarios = InScenarios;
		TFunction<bool()> CloseStep = [Scenarios]()
		{
			for (const FScenarioRef& Scenario : Scenarios)
			{
				if (Scenario->Driver.IsValid())
				{
					Scenario->Driver->Close();
					Scenario->Driver.Reset();
				}
				if (Scenario->DpiPreviewBefore.IsSet())
				{
					GetMutableDefault<UDreamUIDesignerSettings>()->bPreviewDPIScale = Scenario->DpiPreviewBefore.GetValue();
					Scenario->DpiPreviewBefore.Reset();
				}
			}
			return true;
		};
		EnqueueStep(CloseStep);

		TSharedRef<bool> bFrameGone = MakeShared<bool>(false);
		TFunction<bool()> ReleaseStep = [Scenarios, bFrameGone]()
		{
			if (!*bFrameGone)
			{
				*bFrameGone = true;
				return false;
			}
			for (const FScenarioRef& Scenario : Scenarios)
			{
				DreamTests::ReleaseDesignerTestAsset(Scenario->Asset);
			}
			return true;
		};
		EnqueueStep(ReleaseStep);
	}

	/**
	 * Somewhere on the Blueprint root that is also on screen, chosen by the scenario's own stream.
	 *
	 * Inside a margin of the viewport, and inside the root's projected rect when it has one: a drop
	 * off the root still lands (the root is the fallback container), but it would not be a drop an
	 * author makes.
	 */
	FIntPoint RandomCanvasPixel(const FScenarioRef& InScenario)
	{
		if (!InScenario->Driver.IsValid())
		{
			return FIntPoint::ZeroValue;
		}
		DreamTests::FDreamDesignerDriver& Driver = *InScenario->Driver;
		const FIntPoint Size = Driver.ViewportPixelSize();
		FBox2D Area(FVector2D(Size.X * 0.15, Size.Y * 0.15), FVector2D(Size.X * 0.85, Size.Y * 0.85));
		if (const TOptional<FBox2D> RootRect = Driver.WidgetPixelRect(Driver.BlueprintRoot()))
		{
			const FBox2D Clipped(
				FVector2D(FMath::Max(Area.Min.X, RootRect->Min.X + 8.0), FMath::Max(Area.Min.Y, RootRect->Min.Y + 8.0)),
				FVector2D(FMath::Min(Area.Max.X, RootRect->Max.X - 8.0), FMath::Min(Area.Max.Y, RootRect->Max.Y - 8.0)));
			if (Clipped.Max.X > Clipped.Min.X + 1.0 && Clipped.Max.Y > Clipped.Min.Y + 1.0)
			{
				Area = Clipped;
			}
		}
		const double AlphaX = InScenario->Random.FRand();
		const double AlphaY = InScenario->Random.FRand();
		return FIntPoint(
			FMath::RoundToInt32(FMath::Lerp(Area.Min.X, Area.Max.X, AlphaX)),
			FMath::RoundToInt32(FMath::Lerp(Area.Min.Y, Area.Max.Y, AlphaY)));
	}

	/** Drag a Button in from the palette. A refusal is counted and reported at the end, not here. */
	void DropButton(const FScenarioRef& InScenario, FIntPoint InPixel)
	{
		if (!InScenario->Driver.IsValid())
		{
			return;
		}
		if (!InScenario->Driver->DropFromPalette(UDreamButton::StaticClass(), InPixel))
		{
			++InScenario->DropsRefused;
		}
	}

	int32 ButtonsInAsset(const FScenarioRef& InScenario)
	{
		return DreamTests::CountDescendantsOfClass(
			DreamTests::DesignerTemplateRoot(InScenario->Asset.Blueprint), UDreamButton::StaticClass());
	}

	/** The plain claims: open, rooted, whole, and holding what was put in. */
	void CheckStillStanding(FAutomationTestBase& InTest, const FScenarioRef& InScenario, int32 InExpectedButtons, const TCHAR* InWhen)
	{
		if (!InScenario->bAlive || !InScenario->Driver.IsValid())
		{
			InTest.AddError(FString::Printf(TEXT("%s: there is no designer left to look at."), InWhen));
			return;
		}
		DreamTests::FDreamDesignerDriver& Driver = *InScenario->Driver;
		InTest.TestTrue(FString::Printf(TEXT("%s: the toolkit is still the one editing the asset"), InWhen), Driver.IsToolkitOpen());
		UDreamWidget* PreviewRoot = Driver.BlueprintRoot();
		if (InTest.TestNotNull(FString::Printf(TEXT("%s: the preview still has a root"), InWhen), PreviewRoot))
		{
			InTest.TestFalse(FString::Printf(TEXT("%s: and nothing under it is a hole"), InWhen),
				DreamTests::HasNullDescendantUnder(PreviewRoot));
		}
		InTest.TestEqual(FString::Printf(TEXT("%s: the asset holds %d button(s)"), InWhen, InExpectedButtons),
			ButtonsInAsset(InScenario), InExpectedButtons);
		InTest.TestEqual(FString::Printf(TEXT("%s: no drop was refused"), InWhen), InScenario->DropsRefused, 0);
	}

	void CheckFramesDrawn(FAutomationTestBase& InTest, const FScenarioRef& InScenario)
	{
		InTest.TestTrue(FString::Printf(TEXT("The designer viewport was drawn at least %d times; it was drawn %d"),
			MinimumFramesDrawn, InScenario->FramesDrawn), InScenario->FramesDrawn >= MinimumFramesDrawn);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiThirtyButtonsTest,
	"DreamGUI.Designer.RHI.DraggingThirtyButtonsInDrawsEveryFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiThirtyButtonsTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	const FScenarioRef Scenario = BeginScenario(*this, TEXT("DesignerRhiThirtyButtons"));
	EnqueueDrawnFrames({ Scenario }, 2);
	// The gesture the crash was first seen on, thirty times, each followed by two frames the drop has
	// to be drawn in: a drop rebuilds the whole preview, so every one of them hands the renderer a tree
	// it has not seen, in the frame after the one it was last drawing.
	for (int32 Drop = 0; Drop < 30; ++Drop)
	{
		EnqueueAction(Scenario, [Scenario]()
		{
			DropButton(Scenario, RandomCanvasPixel(Scenario));
		});
		EnqueueDrawnFrames({ Scenario }, 2);
	}
	EnqueueCheck([this, Scenario]()
	{
		CheckStillStanding(*this, Scenario, 30, TEXT("After thirty drops"));
		CheckFramesDrawn(*this, Scenario);
	});
	EnqueueTeardown({ Scenario });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiCompileBetweenDragsTest,
	"DreamGUI.Designer.RHI.CompilingBetweenDragsKeepsRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiCompileBetweenDragsTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	const FScenarioRef Scenario = BeginScenario(*this, TEXT("DesignerRhiCompileBetweenDrags"));
	EnqueueDrawnFrames({ Scenario }, 2);
	// Drop and compile in one frame, then draw: compiling re-instances the class every preview widget
	// was built from, so the tree the renderer drew a frame ago is replaced wholesale twice over --
	// once by the drop and once by the compile -- before the next draw.
	for (int32 Cycle = 0; Cycle < 10; ++Cycle)
	{
		EnqueueAction(Scenario, [Scenario]()
		{
			DropButton(Scenario, RandomCanvasPixel(Scenario));
			if (Scenario->Driver.IsValid())
			{
				Scenario->Driver->Compile();
			}
		});
		EnqueueDrawnFrames({ Scenario }, 3);
	}
	EnqueueCheck([this, Scenario]()
	{
		CheckStillStanding(*this, Scenario, 10, TEXT("After ten drops, each compiled"));
		CheckFramesDrawn(*this, Scenario);
	});
	EnqueueTeardown({ Scenario });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiUndoRedoStormTest,
	"DreamGUI.Designer.RHI.UndoRedoStormWhileRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiUndoRedoStormTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	const FScenarioRef Scenario = BeginScenario(*this, TEXT("DesignerRhiUndoRedoStorm"));
	EnqueueDrawnFrames({ Scenario }, 2);
	for (int32 Drop = 0; Drop < 5; ++Drop)
	{
		EnqueueAction(Scenario, [Scenario]()
		{
			DropButton(Scenario, RandomCanvasPixel(Scenario));
		});
		EnqueueDrawnFrames({ Scenario }, 1);
	}
	EnqueueCheck([this, Scenario]()
	{
		CheckStillStanding(*this, Scenario, 5, TEXT("After five drops"));
	});
	// Every undo and every redo rebuilds the preview from the asset, so for ten frames running the
	// tree the renderer was handed last frame is torn down on the game thread before this frame's draw.
	for (int32 Step = 0; Step < 5; ++Step)
	{
		EnqueueAction(Scenario, [Scenario]()
		{
			if (Scenario->Driver.IsValid())
			{
				Scenario->Driver->Undo();
			}
		});
		EnqueueDrawnFrames({ Scenario }, 1);
	}
	EnqueueCheck([this, Scenario]()
	{
		CheckStillStanding(*this, Scenario, 0, TEXT("After five undos"));
	});
	for (int32 Step = 0; Step < 5; ++Step)
	{
		EnqueueAction(Scenario, [Scenario]()
		{
			if (Scenario->Driver.IsValid())
			{
				Scenario->Driver->Redo();
			}
		});
		EnqueueDrawnFrames({ Scenario }, 1);
	}
	EnqueueCheck([this, Scenario]()
	{
		CheckStillStanding(*this, Scenario, 5, TEXT("After five redos"));
		CheckFramesDrawn(*this, Scenario);
	});
	EnqueueTeardown({ Scenario });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiResizeTest,
	"DreamGUI.Designer.RHI.ResizingThePreviewWhileRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiResizeTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	const FScenarioRef Scenario = BeginScenario(*this, TEXT("DesignerRhiResize"));
	EnqueueDrawnFrames({ Scenario }, 2);
	TSharedRef<int32> ResizesRefused = MakeShared<int32>(0);
	const FIntPoint Sizes[] = { FIntPoint(320, 240), FIntPoint(640, 480), FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440) };
	for (const FIntPoint& Size : Sizes)
	{
		// Both sizes at once: the viewport's, which reallocates its render target, and the device
		// resolution the design canvas is laid out for, which resizes the canvas the preview hangs on.
		EnqueueAction(Scenario, [Scenario, Size, ResizesRefused]()
		{
			if (!Scenario->Driver.IsValid())
			{
				return;
			}
			if (!Scenario->Driver->ResizeViewport(Size))
			{
				++(*ResizesRefused);
			}
			if (FDreamWidgetBlueprintEditor* Toolkit = Scenario->Driver->Toolkit())
			{
				Toolkit->SetDesignerViewportSize(Size);
			}
		});
		EnqueueDrawnFrames({ Scenario }, 1);
		EnqueueAction(Scenario, [Scenario]()
		{
			if (Scenario->Driver.IsValid())
			{
				DropButton(Scenario, Scenario->Driver->ViewportCentrePixel());
			}
		});
		EnqueueDrawnFrames({ Scenario }, 3);
	}
	// The DPI preview, both ways. Switched on the settings object rather than through the toolbar's
	// toggle, which saves the author's preferences to disk: a scenario that crashed half-way would
	// leave them changed. What the toggle does besides saving is re-apply the chosen resolution, and
	// that is done here the same way.
	EnqueueAction(Scenario, [Scenario]()
	{
		FDreamWidgetBlueprintEditor* Toolkit = Scenario->Driver.IsValid() ? Scenario->Driver->Toolkit() : nullptr;
		if (Toolkit == nullptr)
		{
			return;
		}
		UDreamUIDesignerSettings* Settings = GetMutableDefault<UDreamUIDesignerSettings>();
		Scenario->DpiPreviewBefore = Settings->bPreviewDPIScale;
		Settings->bPreviewDPIScale = !Settings->bPreviewDPIScale;
		Toolkit->ApplyDesignerViewportSize(Toolkit->GetDesignerViewportSize(), /*bRecordOnAsset*/false);
	});
	EnqueueDrawnFrames({ Scenario }, 3);
	EnqueueAction(Scenario, [Scenario]()
	{
		if (Scenario->Driver.IsValid())
		{
			DropButton(Scenario, Scenario->Driver->ViewportCentrePixel());
		}
	});
	EnqueueDrawnFrames({ Scenario }, 3);
	EnqueueAction(Scenario, [Scenario]()
	{
		FDreamWidgetBlueprintEditor* Toolkit = Scenario->Driver.IsValid() ? Scenario->Driver->Toolkit() : nullptr;
		if (Toolkit == nullptr || !Scenario->DpiPreviewBefore.IsSet())
		{
			return;
		}
		GetMutableDefault<UDreamUIDesignerSettings>()->bPreviewDPIScale = Scenario->DpiPreviewBefore.GetValue();
		Scenario->DpiPreviewBefore.Reset();
		Toolkit->ApplyDesignerViewportSize(Toolkit->GetDesignerViewportSize(), /*bRecordOnAsset*/false);
	});
	EnqueueDrawnFrames({ Scenario }, 3);
	EnqueueCheck([this, Scenario, ResizesRefused]()
	{
		CheckStillStanding(*this, Scenario, 6, TEXT("After five sizes and a DPI round trip"));
		TestEqual(TEXT("Every resize took"), *ResizesRefused, 0);
		if (Scenario->Driver.IsValid() && Scenario->Driver->Toolkit() != nullptr)
		{
			const FIntPoint Chosen = Scenario->Driver->Toolkit()->GetDesignerViewportSize();
			TestTrue(FString::Printf(TEXT("The designer still reports the last resolution chosen, 2560x1440; it reports %dx%d"),
				Chosen.X, Chosen.Y), Chosen == FIntPoint(2560, 1440));
		}
		CheckFramesDrawn(*this, Scenario);
	});
	EnqueueTeardown({ Scenario });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiSwitchModesTest,
	"DreamGUI.Designer.RHI.SwitchingModesWhileRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiSwitchModesTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	const FScenarioRef Scenario = BeginScenario(*this, TEXT("DesignerRhiSwitchModes"));
	EnqueueDrawnFrames({ Scenario }, 2);
	// The viewport is built once per toolkit and re-hosted by whichever tab the Designer mode spawns,
	// so in Graph mode it is still alive, still realtime and still drawn -- with no window around it.
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		EnqueueAction(Scenario, [Scenario]()
		{
			if (FDreamWidgetBlueprintEditor* Toolkit = Scenario->Driver.IsValid() ? Scenario->Driver->Toolkit() : nullptr)
			{
				Toolkit->SetCurrentMode(FDreamWidgetBlueprintApplicationModes::GraphMode);
			}
		});
		EnqueueDrawnFrames({ Scenario }, 3);
		EnqueueAction(Scenario, [Scenario]()
		{
			if (FDreamWidgetBlueprintEditor* Toolkit = Scenario->Driver.IsValid() ? Scenario->Driver->Toolkit() : nullptr)
			{
				Toolkit->SetCurrentMode(FDreamWidgetBlueprintApplicationModes::DesignerMode);
			}
		});
		EnqueueDrawnFrames({ Scenario }, 3);
		EnqueueAction(Scenario, [Scenario]()
		{
			DropButton(Scenario, RandomCanvasPixel(Scenario));
		});
		EnqueueDrawnFrames({ Scenario }, 3);
	}
	EnqueueCheck([this, Scenario]()
	{
		CheckStillStanding(*this, Scenario, 3, TEXT("After three trips to the graph and back"));
		if (Scenario->Driver.IsValid() && Scenario->Driver->Toolkit() != nullptr)
		{
			TestTrue(TEXT("The toolkit is back in Designer mode"),
				Scenario->Driver->Toolkit()->GetCurrentMode() == FDreamWidgetBlueprintApplicationModes::DesignerMode);
		}
		CheckFramesDrawn(*this, Scenario);
	});
	EnqueueTeardown({ Scenario });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiTwoDesignersTest,
	"DreamGUI.Designer.RHI.TwoDesignersOpenAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiTwoDesignersTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	const FScenarioRef First = BeginScenario(*this, TEXT("DesignerRhiTwoDesignersFirst"));
	const FScenarioRef Second = BeginScenario(*this, TEXT("DesignerRhiTwoDesignersSecond"));
	const TArray<FScenarioRef> Both = { First, Second };
	EnqueueDrawnFrames(Both, 2);
	// Two preview worlds, two views of the designer's render path in every frame, and the edits
	// alternating between them: whatever the renderer keeps per frame is being asked for twice.
	for (int32 Cycle = 0; Cycle < 5; ++Cycle)
	{
		EnqueueAction(First, [First]()
		{
			DropButton(First, RandomCanvasPixel(First));
		});
		EnqueueDrawnFrames(Both, 2);
		EnqueueAction(Second, [Second]()
		{
			DropButton(Second, RandomCanvasPixel(Second));
		});
		EnqueueDrawnFrames(Both, 2);
	}
	EnqueueCheck([this, First, Second]()
	{
		CheckStillStanding(*this, First, 5, TEXT("The first designer, after five drops"));
		CheckStillStanding(*this, Second, 5, TEXT("The second designer, after five drops"));
	});
	// One goes away while the other keeps drawing and keeps being edited.
	EnqueueAction(First, [First]()
	{
		if (First->Driver.IsValid())
		{
			First->Driver->Close();
			First->Driver.Reset();
		}
	});
	EnqueueDrawnFrames({ Second }, 2);
	EnqueueAction(Second, [Second]()
	{
		DropButton(Second, RandomCanvasPixel(Second));
	});
	EnqueueDrawnFrames({ Second }, 3);
	EnqueueCheck([this, First, Second]()
	{
		CheckStillStanding(*this, Second, 6, TEXT("The second designer, after the first closed"));
		TestEqual(TEXT("The first asset still holds its five buttons"), ButtonsInAsset(First), 5);
		CheckFramesDrawn(*this, Second);
	});
	EnqueueTeardown(Both);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiCloseAndReopenTest,
	"DreamGUI.Designer.RHI.CloseAndReopenTenTimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiCloseAndReopenTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	// One asset, opened ten times: each open builds a preview world and a viewport from scratch, and
	// each close tears them down while the last frame drawn in them may still be on the render thread.
	const FScenarioRef Scenario = BeginScenario(*this, TEXT("DesignerRhiCloseAndReopen"), /*bOpen*/false);
	for (int32 Round = 0; Round < 10; ++Round)
	{
		EnqueueAction(Scenario, [this, Scenario]()
		{
			if (!Scenario->Driver.IsValid() && !OpenDesigner(*this, Scenario))
			{
				Scenario->bAlive = false;
			}
		});
		// Drawn once before the first edit, so every visit renders the preview it opened on as well as
		// the one the drop replaces it with.
		EnqueueDrawnFrames({ Scenario }, 1);
		EnqueueAction(Scenario, [Scenario]()
		{
			if (Scenario->Driver.IsValid())
			{
				DropButton(Scenario, Scenario->Driver->ViewportCentrePixel());
			}
		});
		EnqueueDrawnFrames({ Scenario }, 2);
		EnqueueAction(Scenario, [Scenario]()
		{
			if (Scenario->Driver.IsValid())
			{
				Scenario->Driver->Close();
				Scenario->Driver.Reset();
			}
		});
		// The close is deferred to the next frame; opening again inside this one would find the
		// toolkit that is on its way out.
		EnqueueDrawnFrames({ Scenario }, 1);
	}
	EnqueueCheck([this, Scenario]()
	{
		TestEqual(TEXT("The designer opened every time"), Scenario->OpensSucceeded, 10);
		TestEqual(TEXT("The asset holds one button per visit"), ButtonsInAsset(Scenario), 10);
		TestEqual(TEXT("No drop was refused"), Scenario->DropsRefused, 0);
		CheckFramesDrawn(*this, Scenario);
	});
	EnqueueTeardown({ Scenario });
	return true;
}

/*
 * A click on the backdrop -- the viewport outside the design canvas, where there is no widget to hit --
 * clears the selection. Here rather than with the other pointer probes because the designer only gets
 * as far as clearing once the engine has looked the pixel up in the viewport's hit-proxy map and found
 * nothing, and under -nullrhi that map reads back as zeros: the id of whatever hit proxy owns slot zero.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerRhiBackdropClickTest,
	"DreamGUI.Designer.RHI.ClickingTheBackdropClearsTheSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDesignerRhiBackdropClickTest::RunTest(const FString&)
{
	using namespace DreamDesignerRhiScenarioLocal;

	LogRenderGraphSettings(*this);
	const FScenarioRef Scenario = BeginScenario(*this, TEXT("DesignerRhiBackdropClick"));
	EnqueueDrawnFrames({ Scenario }, 2);
	TSharedRef<FIntPoint> Backdrop = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
	EnqueueAction(Scenario, [this, Scenario, Backdrop]()
	{
		DreamTests::FDreamDesignerDriver& Driver = *Scenario->Driver;
		FDreamWidgetBlueprintEditor* Toolkit = Driver.Toolkit();
		const TOptional<FBox2D> CanvasRect = Driver.WidgetPixelRect(Driver.PreviewRoot());
		const float PixelsPerUnit = Toolkit != nullptr ? Toolkit->GetDesignerPixelsPerUnit() : 0.0f;
		const FIntPoint Size = Driver.ViewportPixelSize();
		if (Toolkit == nullptr || !CanvasRect.IsSet() || PixelsPerUnit <= 0.0f)
		{
			AddError(TEXT("The design canvas cannot be measured on screen, so no backdrop can be found."));
			Scenario->bAlive = false;
			return;
		}
		// Zoom so the design canvas takes half the viewport on its longer side: there is then backdrop
		// on every side of it, whatever size the canvas is.
		const double Shrink = 0.5 * FMath::Min(
			Size.X / FMath::Max(CanvasRect->Max.X - CanvasRect->Min.X, 1.0),
			Size.Y / FMath::Max(CanvasRect->Max.Y - CanvasRect->Min.Y, 1.0));
		Toolkit->SetDesignerPixelsPerUnit(static_cast<float>(PixelsPerUnit * Shrink));
		DropButton(Scenario, Driver.ViewportCentrePixel());
		*Backdrop = FIntPoint(Size.X - 8, Size.Y - 8);
	});
	EnqueueDrawnFrames({ Scenario }, 3);
	EnqueueAction(Scenario, [this, Scenario, Backdrop]()
	{
		DreamTests::FDreamDesignerDriver& Driver = *Scenario->Driver;
		UDreamWidget* Dropped = DreamTests::FirstLiveChildOf(Driver.BlueprintRoot());
		const TOptional<FBox2D> CanvasRect = Driver.WidgetPixelRect(Driver.PreviewRoot());
		if (!TestNotNull(TEXT("The button arrived under the Blueprint root"), Dropped)
			|| !TestTrue(TEXT("The design canvas is on screen"), CanvasRect.IsSet()))
		{
			Scenario->bAlive = false;
			return;
		}
		if (!TestFalse(FString::Printf(TEXT("The backdrop pixel (%d, %d) is outside the design canvas %s"),
			Backdrop->X, Backdrop->Y, *CanvasRect->ToString()), CanvasRect->IsInside(FVector2D(*Backdrop))))
		{
			Scenario->bAlive = false;
			return;
		}
		Driver.Toolkit()->SelectWidgets(TSet<UDreamWidget*>({ Dropped }), /*bAppendOrToggle*/false);
		// Pressed now and released a frame later, with the engine's own tick in between, the way a
		// hand's click spans frames. The press misses every handle of the selection, so it arms a
		// marquee that never travels and the release is the engine's click.
		Driver.MoveTo(*Backdrop);
		Driver.Press(EKeys::LeftMouseButton);
	});
	EnqueueDrawnFrames({ Scenario }, 1);
	EnqueueAction(Scenario, [this, Scenario]()
	{
		TestEqual(TEXT("The button is still the selection while the button is held"), Scenario->Driver->SelectedWidgets().Num(), 1);
		Scenario->Driver->Release(EKeys::LeftMouseButton);
	});
	EnqueueDrawnFrames({ Scenario }, 4);
	EnqueueCheck([this, Scenario]()
	{
		if (Scenario->bAlive && Scenario->Driver.IsValid())
		{
			TestEqual(TEXT("A click on the backdrop leaves nothing selected"), Scenario->Driver->SelectedWidgets().Num(), 0);
			CheckFramesDrawn(*this, Scenario);
		}
	});
	EnqueueTeardown({ Scenario });
	return true;
}

#endif
