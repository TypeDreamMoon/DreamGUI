// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

// From the module's Private root, which is the only one on the include path.
#include "Driver/Designer/DreamDesignerDriver.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * THE WAY OUT.
 *
 * What the editor's own exit meets, on the road a person takes to it: a designer opened and closed, a preview
 * shut down twice over, a play session started and stopped -- and then the exit. The test is the first half. The
 * Exit preset runs it in an editor it then quits the way a person does (the automation SoftQuit, not -TestExit's
 * forced exit) and searches that exit's log for what a teardown must never leave; every ensure along the way fails
 * the run as well. Everything here is closed before the exit, as a person closes it: what the exit finds is what
 * the closing left behind.
 */
namespace DreamLifecycleSmokeLocal
{
	/** What the latent steps carry between them. */
	struct FSmokeState
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		TSharedPtr<DreamTests::FDreamDesignerDriver> Driver;
	};

	void EnqueueStep(TFunction<bool()> InStep)
	{
		// Through a named local: a lambda's capture list carries commas, and the macro would cut its argument there.
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleSmokeTest,
	"DreamGUI.Lifecycle.Smoke.ADesignerAPreviewAndAPlaySessionAllComeAndGoBeforeTheEditorExits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleSmokeLocal;
	TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	State->Package = CreatePackage(TEXT("/Temp/DreamGUITests/LifecycleSmoke"));
	State->Package->AddToRoot();
	State->Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UDreamUserWidget::StaticClass(), State->Package, FName(TEXT("LifecycleSmoke")), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("A widget blueprint"), State->Blueprint))
	{
		State->Package->RemoveFromRoot();
		return false;
	}
	State->Blueprint->GetOrCreateWidgetTree()->RootWidget->SetDisplayName(TEXT("Root"));
	FKismetEditorUtilities::CompileBlueprint(State->Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);

	// A preview, built, then shut down twice: the second is nothing, as every owner's teardown has to be -- an owner
	// is shut down by its owner, and again by its own destructor.
	{
		const TSharedRef<FDreamWidgetPreviewHost> Preview = MakeShared<FDreamWidgetPreviewHost>();
		Preview->Initialize(State->Blueprint);
		TestNotNull(TEXT("The preview built a world"), Preview->GetWorld());
		TestNotNull(TEXT("...and the class's instance in it"), Preview->GetPreviewWidget());
		Preview->Shutdown();
		Preview->Shutdown();
		TestNull(TEXT("Shut down twice over, it holds no world"), Preview->GetWorld());
	}

	// The designer, opened, left a couple of frames, and closed -- the close is deferred, so the asset is let go of a
	// frame after it.
	EnqueueStep([this, State]()
	{
		State->Driver = DreamTests::FDreamDesignerDriver::Open(State->Blueprint);
		TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
		return true;
	});
	EnqueueFrames(2);
	EnqueueStep([State]()
	{
		if (State->Driver.IsValid())
		{
			State->Driver->Close();
			State->Driver.Reset();
		}
		return true;
	});
	EnqueueFrames(2);
	EnqueueStep([State]()
	{
		if (State->Package != nullptr)
		{
			State->Package->RemoveFromRoot();
			State->Package = nullptr;
		}
		State->Blueprint = nullptr;
		return true;
	});

	// A play session with a button on its screen, started and stopped.
	const TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([](FDreamDriverPieRig& InRig)
	{
		InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0));
	});
	Rig->Sequence().WaitFrames(2).PerformLatent();
	Rig->Finish();
	return true;
}

#endif
