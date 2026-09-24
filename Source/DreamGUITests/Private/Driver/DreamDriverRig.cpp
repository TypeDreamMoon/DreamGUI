// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverRig.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Interaction/UITextInput.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"

#include "Driver/DreamDriverInputModule.h"
#include "DreamScopedGameInstanceWorld.h"
#include "DreamScopedWorld.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamDriverRig, Log, All);

FDreamDriverRig FDreamDriverRig::Headless(FIntPoint InViewportSize)
{
	FDreamRigOptions ViewportOnly;
	ViewportOnly.ViewportSize = InViewportSize;
	return FDreamDriverRig(ViewportOnly);
}

FDreamDriverRig FDreamDriverRig::Headless(const FDreamRigOptions& InOptions)
{
	return FDreamDriverRig(InOptions);
}

FDreamDriverRig::FDreamDriverRig(const FDreamRigOptions& InOptions)
	: Options(InOptions)
{
	const FIntPoint InViewportSize = Options.ViewportSize;
	DriverContext = MakeUnique<FDreamDriverContext>();
	// Created unconditionally, even if the build below goes wrong, so Driver() is always answerable
	// and a test that forgot to check IsUsable fails on an assertion rather than on a null driver.
	DriverInstance = MakeShared<FDreamDriver>(*DriverContext);

	// Before anything is built, because building is what disturbs them: the process-wide switches a
	// text field flips the first time it is typed into outlive every world, and a rig that left them
	// flipped would decide which road the NEXT test's characters take.
	CaptureProcessState();

	// 1. The world. With a game instance by default, because the tween manager is a game instance
	// subsystem: without one every UDreamTweenManager::To answers null, every Selectable transition
	// silently snaps or never happens, and the controls are being tested in the designer's preview
	// world rather than in a game's. The bare world stays available for exactly that comparison.
	UWorld* BuildWorld = nullptr;
	if (Options.bWithGameInstance)
	{
		ScopedGameInstanceWorld = MakeUnique<DreamTests::FScopedGameInstanceWorld>();
		BuildWorld = ScopedGameInstanceWorld->World;
		DriverContext->GameInstance = ScopedGameInstanceWorld->GameInstance;
		if (BuildWorld == nullptr)
		{
			BuildFailure = TEXT("UGameInstance::InitializeStandalone did not give the game instance a world");
			return;
		}
	}
	else
	{
		ScopedWorld = MakeUnique<DreamTests::FScopedGameWorld>(EWorldType::Game);
		BuildWorld = ScopedWorld->World;
		if (BuildWorld == nullptr)
		{
			BuildFailure = TEXT("UWorld::CreateWorld did not make a world");
			return;
		}
	}
	DriverContext->World = BuildWorld;
	DriverContext->Manager = UDreamUIManagerWorldSubsystem::GetInstance(BuildWorld);

	Host = BuildWorld->SpawnActor<AActor>();
	if (Host == nullptr)
	{
		BuildFailure = TEXT("the world would not spawn the rig's host actor");
		return;
	}

	UDreamEventSystem* BuiltEventSystem = NewObject<UDreamEventSystem>(Host);
	// AddInstanceComponent before RegisterComponent, the way the world-space raycast fixture does it:
	// it is what makes the component belong to the actor rather than merely be outered to it.
	Host->AddInstanceComponent(BuiltEventSystem);
	BuiltEventSystem->RegisterComponent();
	DriverContext->EventSystem = BuiltEventSystem;

	UDreamDriverInputModule* BuiltInputModule = NewObject<UDreamDriverInputModule>(Host);
	Host->AddInstanceComponent(BuiltInputModule);
	BuiltInputModule->RegisterComponent();
	BuiltInputModule->RegisterInputModuleToEventSystem(BuiltEventSystem);
	DriverContext->InputModule = BuiltInputModule;

	UDreamWidget* BuiltRoot = NewObject<UDreamWidget>(BuildWorld, NAME_None, RF_Public | RF_Transactional);
	BuiltRoot->SetDisplayName(TEXT("Root"));
	BuiltRoot->OnRegister();
	DriverContext->Root = BuiltRoot;

	UDreamCanvas* BuiltCanvas = BuiltRoot->AddComponent<UDreamCanvas>();
	if (BuiltCanvas == nullptr)
	{
		BuildFailure = TEXT("the root widget would not take a canvas");
		return;
	}
	BuiltCanvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	// AFTER the render mode. Setting the mode applies the viewport parameters, and at that moment the
	// only viewport there is is the 2x2 fallback; handing the canvas a real size is what un-does that,
	// and doing it in the other order would leave the fallback applied on top.
	BuiltCanvas->SetViewportSizeOverride(InViewportSize);
	// The scaler AFTER the viewport, through the canvas's own setters: each of them re-runs
	// OnViewportParameterChanged, which recomputes the root's size and CanvasScale from the viewport
	// size the canvas has cached -- so that has to be the substituted one already. The mode goes
	// last, once the reference and the match it will read are in place; nothing is written when the
	// options leave the mode unset, which keeps the canvas's own default (ConstantPixelSize) and
	// today's rig exactly.
	if (Options.CanvasScaleMode.IsSet())
	{
		BuiltCanvas->SetReferenceResolution(Options.ReferenceResolution);
		BuiltCanvas->SetMatchFromWidthToHeight(Options.MatchFromWidthToHeight);
		BuiltCanvas->SetScaleMode(Options.CanvasScaleMode.GetValue());
	}
	DriverContext->RootCanvas = BuiltCanvas;

	UDreamScreenSpaceRaycaster* BuiltRaycaster = NewObject<UDreamScreenSpaceRaycaster>(Host);
	BuiltRaycaster->SetRootCanvas(BuiltCanvas);
	Host->AddInstanceComponent(BuiltRaycaster);
	BuiltRaycaster->RegisterComponent();
	// Explicit, although registering an auto-activating component normally gets here by itself: the
	// list this puts it on is the one UDreamPointerInputModule::LineTrace walks, so a rig that was
	// not on it would trace nothing and every test would fail identically and unhelpfully. Enrolling
	// twice is a no-op -- AddRaycaster refuses duplicates.
	BuiltRaycaster->ActivateRaycaster();
	DriverContext->Raycaster = BuiltRaycaster;

	// Two settling frames before anyone touches it. The first runs the layout the attach queued, the
	// second gives anything the first dirtied its own pass -- which is the one-pass convergence the
	// manager's own counter calls healthy. Without them the first action would hit-test a tree whose
	// widgets are all still at the origin.
	// Everything the world starts with exists; now it begins play, before any control is made on it.
	OpenBeginPlayGate();

	DriverContext->PumpFrames(2);

	if (!IsUsable() && BuildFailure.IsEmpty())
	{
		// Every early return above says why; this is the net under a piece that came back null or
		// invalid without anything having refused outright.
		BuildFailure = TEXT("the rig was built but a piece it needs is missing or invalid (world, event system, input module, UI manager, root, canvas or raycaster)");
	}
}

void FDreamDriverRig::CaptureProcessState()
{
	// Only the switch. The field being edited is not captured, because it is not restored: see
	// EndLeakedTextEdit for why, and for what is guaranteed about it instead.
	bHostDeliveredCharacterEventsAtStart = UUITextInput::IsHostDeliveringCharacterEvents();
	WatchBlueprintCompiles();
}

#if WITH_EDITOR
namespace DreamDriverRigCompileLog
{
	/**
	 * The editor's Blueprint compile announcements, for the rest of the session from the first rig on.
	 *
	 * The compiling flag checked at tear-down is process state, and so is whatever set it: the compile
	 * that leaves it set has as often as not happened before the rig was built -- in a designer test
	 * just ahead of it, say -- and a rig that heard only what happened while it was up could say
	 * neither which Blueprint that was nor whether its end was ever announced. So the announcements
	 * are heard once for the session, and the last few kept with the frame and the test they came in.
	 */
	struct FAnnouncement
	{
		/** OnBlueprintCompiled names no Blueprint; OnBlueprintPreCompile does. */
		bool bFinished = false;
		FString Blueprint;
		uint64 Frame = 0;
		FString DuringTest;
	};

	/** Enough to show what came just before a rig, without keeping the session's worth. */
	constexpr int32 KeptAnnouncements = 8;

	TArray<FAnnouncement> Recent;
	FDelegateHandle PreCompileHandle;
	FDelegateHandle CompiledHandle;
	FDelegateHandle EnginePreExitHandle;
	uint64 ListeningSinceFrame = 0;

	void Record(bool bInFinished, const UBlueprint* InBlueprint)
	{
		if (Recent.Num() >= KeptAnnouncements)
		{
			Recent.RemoveAt(0);
		}
		FAnnouncement& Announcement = Recent.AddDefaulted_GetRef();
		Announcement.bFinished = bInFinished;
		Announcement.Blueprint = InBlueprint != nullptr ? InBlueprint->GetPathName() : FString();
		Announcement.Frame = GFrameCounter;
		const FAutomationTestBase* Running = FAutomationTestFramework::Get().GetCurrentTest();
		Announcement.DuringTest = Running != nullptr ? Running->GetTestFullName() : FString(TEXT("no test"));
	}

	void EnsureListening()
	{
		if (GEditor == nullptr || PreCompileHandle.IsValid())
		{
			return;
		}
		ListeningSinceFrame = GFrameCounter;
		PreCompileHandle = GEditor->OnBlueprintPreCompile().AddLambda([](UBlueprint* InBlueprint)
		{
			Record(false, InBlueprint);
		});
		CompiledHandle = GEditor->OnBlueprintCompiled().AddLambda([]()
		{
			Record(true, nullptr);
		});
		// Taken off again before the editor goes away: the two lambdas are code in this module.
		EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddLambda([]()
		{
			if (GEditor != nullptr)
			{
				GEditor->OnBlueprintPreCompile().Remove(PreCompileHandle);
				GEditor->OnBlueprintCompiled().Remove(CompiledHandle);
			}
			PreCompileHandle.Reset();
			CompiledHandle.Reset();
		});
	}

	FString Describe(const FAnnouncement& InAnnouncement)
	{
		return InAnnouncement.bFinished
			? FString::Printf(TEXT("frame %llu, a compile announced finished (during %s)"), InAnnouncement.Frame, *InAnnouncement.DuringTest)
			: FString::Printf(TEXT("frame %llu, %s began compiling (during %s)"), InAnnouncement.Frame, *InAnnouncement.Blueprint, *InAnnouncement.DuringTest);
	}

	/**
	 * What was heard, said for a compiling flag found set at tear-down -- which is always a leak.
	 *
	 * UDreamUIManagerObject sets the flag on OnBlueprintPreCompile and clears it on OnBlueprintCompiled
	 * itself, and the editor pairs the two for every compile, failed ones included. So a flag still set
	 * once the rig is down is a compile the editor never announced as finished, and the sentence names
	 * the Blueprint that began it, the frame and the test it came in, whether the flag was already set
	 * when the rig was built -- a compile left open by an earlier test -- and the announcements before.
	 */
	FString DescribeSetFlag(bool bInSetWhenBuilt, uint64 InBuiltAtFrame)
	{
		const FAnnouncement* Last = Recent.Num() > 0 ? &Recent.Last() : nullptr;
		FString Why;
		if (Last == nullptr)
		{
			Why = FString::Printf(TEXT("The editor's Blueprint compiling flag is set and no compile has been announced since the rigs began listening at frame %llu."),
				ListeningSinceFrame);
		}
		else if (Last->bFinished)
		{
			// Not expected: the clear runs inside the very broadcast recorded here. Said as it is, so a
			// change to that ordering shows up as itself rather than as a mystery.
			Why = FString::Printf(TEXT("The editor's Blueprint compiling flag is set although the last announcement heard, at frame %llu during %s, was a compile finishing."),
				Last->Frame, *Last->DuringTest);
		}
		else
		{
			Why = FString::Printf(TEXT("The editor's Blueprint compiling flag is set: %s began compiling at frame %llu, during %s, and no compile has been announced finished since."),
				*Last->Blueprint, Last->Frame, *Last->DuringTest);
		}
		TArray<FString> Heard;
		for (const FAnnouncement& Announcement : Recent)
		{
			Heard.Add(Describe(Announcement));
		}
		Why += FString::Printf(TEXT(" The flag was %s when this rig was built, at frame %llu. Heard, oldest first: %s."),
			bInSetWhenBuilt ? TEXT("already set") : TEXT("clear"), InBuiltAtFrame,
			Heard.Num() > 0 ? *FString::Join(Heard, TEXT("; ")) : TEXT("nothing"));
		return Why;
	}
}
#endif

void FDreamDriverRig::WatchBlueprintCompiles()
{
#if WITH_EDITOR
	DreamDriverRigCompileLog::EnsureListening();
	bBlueprintCompilingWhenBuilt = UDreamUIManagerObject::GetIsBlueprintCompiling();
	BuiltAtFrame = GFrameCounter;
#endif
}

TArray<FString> FDreamDriverRig::DescribeUnsettledProcessState(int32 InLayoutPassDepth, int32 InDesiredSizeMemoDepth, bool bInBlueprintCompiling)
{
	TArray<FString> Complaints;
	if (InLayoutPassDepth != 0)
	{
		Complaints.Add(FString::Printf(TEXT("UDreamWidget's layout pass depth is %d after the rig was torn down; a layout pass was entered and never left, so every later size edit is being taken for layout output"), InLayoutPassDepth));
	}
	if (InDesiredSizeMemoDepth != 0)
	{
		Complaints.Add(FString::Printf(TEXT("UDreamPanelLayoutBase's desired-size memo depth is %d after the rig was torn down; the memo is still live, so later measurements can be answered from a pass that is over"), InDesiredSizeMemoDepth));
	}
	if (bInBlueprintCompiling)
	{
		Complaints.Add(TEXT("UDreamUIManagerObject still believes a Blueprint is compiling after the rig was torn down; everything that defers itself during a compile will keep deferring"));
	}
	return Complaints;
}

UUITextInput* FDreamDriverRig::FindEditInRigTree() const
{
	UUITextInput* Active = UUITextInput::GetActiveTextInput();
	UDreamWidget* RigRoot = DriverContext.IsValid() ? DriverContext->Root : nullptr;
	if (Active == nullptr || !IsValid(RigRoot))
	{
		return nullptr;
	}
	// Only a field under the rig's own root. A tree built elsewhere in the world -- a world-space
	// panel is its own root -- goes down with the world, not with this root, so its edit is still
	// legitimately open at this point and is checked after the world instead.
	for (UDreamWidget* Walk = Active->GetWidget(); Walk != nullptr; Walk = Walk->GetParent())
	{
		if (Walk == RigRoot)
		{
			return Active;
		}
	}
	return nullptr;
}

void FDreamDriverRig::EndLeakedTextEdit(UUITextInput* InEditInRigTree, FAutomationTestBase* InTest)
{
	// The field being edited is not put back to what it was before the rig: its only writer is
	// ActivateInput, and reviving a field from before this rig -- from another test's world, most
	// likely already gone -- is not something a tear-down should do. What is guaranteed is weaker and
	// is the part that matters: the process-wide pointer does not name anything of THIS rig's once
	// it is gone. The rig's tree has been destroyed by the time this runs, which ends an edit on
	// unregister (UUITextInput::OnUnregister); the field that was being edited in it and is still
	// named here is one that tear-down missed, which is a runtime fault to report, and the edit is
	// ended through the field's own DeactivateInput -- while its world still stands -- so it cannot
	// route the next test's characters into a dead world.
	if (InEditInRigTree != nullptr && UUITextInput::GetActiveTextInput() == InEditInRigTree)
	{
		ReportRigProblem(InTest, TEXT("A text field of this rig was still the active text input after its tree was destroyed; its edit has been ended here so it does not leak into the next test"));
		InEditInRigTree->DeactivateInput(false);
	}
}

void FDreamDriverRig::ReportTextEditOutlivingWorld(const UWorld* InRigWorld, FAutomationTestBase* InTest)
{
	// After the world: every tree in it has gone down with the UI manager (its Deinitialize destroys
	// the registered trees), a world-space panel's included, and every edit with them. The weak
	// pointer answers null for a field the world took with it, so anything still named here survived
	// its own world -- reported, and left alone, because there is no longer a world to end it in.
	UUITextInput* StillActive = UUITextInput::GetActiveTextInput();
	if (StillActive != nullptr && InRigWorld != nullptr && StillActive->GetWorld() == InRigWorld)
	{
		ReportRigProblem(InTest, TEXT("A text field of this rig's world is still the active text input after the world was destroyed"));
	}
}

void FDreamDriverRig::RestoreAndVerifyProcessState(FAutomationTestBase* InTest)
{
	// Put back what the rig found. The character switch is class-wide and flipped for good by the
	// first HandleCharacterInput, so without this a test that types would silently move every test
	// after it off the key-to-character road -- the road a project without a character-delivering
	// viewport client is actually on.
	UUITextInput::SetHostDeliversCharacterEventsForTesting(bHostDeliveredCharacterEventsAtStart);

	// Last, once the tree and the world are both gone, because these are counters the rig's whole
	// life could have moved: a layout pass or a desired-size memo scope that was entered and never
	// left, or a compile the editor object never heard finish. Each of them outlives worlds and would
	// quietly change how the next test's layout behaves.
	const int32 MemoDepth = UDreamPanelLayoutBase::GetDesiredSizeMemoDepthForTesting();
	bool bCompiling = false;
#if WITH_EDITOR
	// Set at all is the leak: the editor object clears it on the announcement that a compile is over,
	// so nothing is still queued by now. Reported, with what was heard and in which test, whether the
	// compile began while the rig was up or before it was built (DreamDriverRigCompileLog).
	if (UDreamUIManagerObject::GetIsBlueprintCompiling())
	{
		bCompiling = true;
		const FString Why = DreamDriverRigCompileLog::DescribeSetFlag(bBlueprintCompilingWhenBuilt, BuiltAtFrame);
		if (InTest != nullptr)
		{
			InTest->AddInfo(Why);
		}
		else
		{
			UE_LOG(LogDreamDriverRig, Display, TEXT("%s"), *Why);
		}
	}
#endif
	for (const FString& Complaint : DescribeUnsettledProcessState(UDreamWidget::GetLayoutPassDepthForTesting(), MemoDepth, bCompiling))
	{
		ReportRigProblem(InTest, Complaint);
	}
}

void FDreamDriverRig::ReportRigProblem(FAutomationTestBase* InTest, const FString& InMessage)
{
	// The bound test when there is one, which is where the failure belongs; otherwise the log at
	// Error, which the automation framework still turns into a failure of whatever test is running.
	if (InTest != nullptr)
	{
		InTest->AddError(InMessage);
	}
	else
	{
		UE_LOG(LogDreamDriverRig, Error, TEXT("%s"), *InMessage);
	}
}

void FDreamDriverRig::OpenBeginPlayGate()
{
	UWorld* HostWorld = DriverContext.IsValid() ? DriverContext->World : nullptr;
	UDreamUIManagerWorldSubsystem* HostManager = DriverContext.IsValid() ? DriverContext->Manager : nullptr;
	if (HostWorld == nullptr || !IsValid(HostManager))
	{
		return;
	}

	// The event system's half: what UDreamEventSystem::BeginPlay does when a world begins play is
	// enrol with the UI manager, and that enrolment is all it does. It is done here directly rather
	// than by calling the component's BeginPlay, which would also mark it begun in a world that is not
	// -- and it is guarded, so EnsureGameInputHost, which makes the same call, stays a no-op after it.
	UDreamEventSystem* HostEventSystem = DriverContext->EventSystem;
	if (IsValid(HostEventSystem)
		&& HostManager->GetEventSystemByUserIndex(HostEventSystem->GetUserIndex()) != HostEventSystem)
	{
		HostManager->AddEventSystem(HostEventSystem);
	}

	// The UI manager's half: OnWorldBeginPlay begins every registered widget that has not begun,
	// which at this point is the root and its canvas and nothing else. Once, because the engine base
	// ensures on a second call and a widget's BeginPlay checks it has not begun -- HasBegunPlay is the
	// guard for both. From here RegisterDreamWidgetHierarchy (MakeControl's road) begins every control
	// it registers, and the pump's TickDreamUI runs their Start and Tick; nothing else drives either.
	if (!HostManager->HasBegunPlay())
	{
		HostManager->OnWorldBeginPlay(*HostWorld);
	}
}

FDreamDriverRig::~FDreamDriverRig()
{
	// Taken before the context goes: the checks at the very end report against the test that was
	// running, and by then the context that knew it has been released.
	FAutomationTestBase* TeardownTest = DriverContext.IsValid() ? DriverContext->CurrentTest : nullptr;
	UWorld* RigWorld = DriverContext.IsValid() ? DriverContext->World : nullptr;

	// Reverse order, and the widget tree before the world: DestroyWidget unregisters the tree from
	// the UI manager while the world is still whole, which is where the manager expects to be told.
	if (DriverContext.IsValid())
	{
		// Asked before the tree goes, while "is it under the rig's root" still has an answer.
		UUITextInput* EditInRigTree = FindEditInRigTree();
		if (UDreamWidget* RootWidget = DriverContext->Root; IsValid(RootWidget))
		{
			RootWidget->DestroyWidget();
		}
		if (UDreamScreenSpaceRaycaster* RigRaycaster = DriverContext->Raycaster; IsValid(RigRaycaster))
		{
			RigRaycaster->DeactivateRaycaster();
		}
		if (UDreamDriverInputModule* RigInputModule = DriverContext->InputModule; IsValid(RigInputModule))
		{
			RigInputModule->UnregisterInputModuleFromEventSystem();
		}
		// After the tree, before the world: the tree's destruction is what should have ended an edit
		// in it, and the world still being whole is what lets a missed one be ended properly.
		EndLeakedTextEdit(EditInRigTree, TeardownTest);
	}
	DriverInstance.Reset();
	DriverContext.Reset();
	// Exactly one of these holds the world; the game instance one also shuts its game instance down
	// and takes its world context off the engine's list.
	ScopedWorld.Reset();
	ScopedGameInstanceWorld.Reset();

	ReportTextEditOutlivingWorld(RigWorld, TeardownTest);
	RestoreAndVerifyProcessState(TeardownTest);
}

bool FDreamDriverRig::IsUsable() const
{
	// A recorded failure wins even if every pointer happens to be set: a half-built input host can
	// leave an event system behind it and still not be a host anything should be driven through.
	return BuildFailure.IsEmpty()
		&& DriverContext.IsValid()
		&& DriverContext->IsUsable()
		&& IsValid(DriverContext->RootCanvas)
		&& IsValid(DriverContext->Raycaster);
}

UWorld* FDreamDriverRig::GetWorld() const
{
	return DriverContext.IsValid() ? DriverContext->World : nullptr;
}

UDreamWidget* FDreamDriverRig::Root() const
{
	return DriverContext.IsValid() ? DriverContext->Root : nullptr;
}

UDreamCanvas* FDreamDriverRig::RootCanvas() const
{
	return DriverContext.IsValid() ? DriverContext->RootCanvas : nullptr;
}

UDreamEventSystem* FDreamDriverRig::EventSystem() const
{
	return DriverContext.IsValid() ? DriverContext->EventSystem : nullptr;
}

UDreamDriverInputModule* FDreamDriverRig::InputModule() const
{
	return DriverContext.IsValid() ? DriverContext->InputModule : nullptr;
}

UDreamScreenSpaceRaycaster* FDreamDriverRig::Raycaster() const
{
	return DriverContext.IsValid() ? DriverContext->Raycaster : nullptr;
}

FDreamDriverContext& FDreamDriverRig::Context() const
{
	return *DriverContext;
}

FDreamDriverRef FDreamDriverRig::Driver() const
{
	return DriverInstance.ToSharedRef();
}

const FDreamRigOptions& FDreamDriverRig::GetOptions() const
{
	return Options;
}

UGameInstance* FDreamDriverRig::GetGameInstance() const
{
	return DriverContext.IsValid() ? DriverContext->GameInstance : nullptr;
}

const FString& FDreamDriverRig::GetBuildFailure() const
{
	return BuildFailure;
}

void FDreamDriverRig::BindTest(FAutomationTestBase* InTest)
{
	if (DriverContext.IsValid())
	{
		DriverContext->CurrentTest = InTest;
	}
}

UDreamWidget* FDreamDriverRig::MakeWidget(const FString& InDisplayName, UDreamWidget* InParent,
	const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr)
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : DriverContext->Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}

	UDreamWidget* NewWidget = NewObject<UDreamWidget>(DriverContext->World, NAME_None, RF_Public | RF_Transactional);
	NewWidget->SetDisplayName(InDisplayName);
	NewWidget->SetWidth(InSize.X);
	NewWidget->SetHeight(InSize.Y);
	NewWidget->OnRegister();
	NewWidget->TrySetParent(Parent, false);
	NewWidget->SetAnchoredPosition(InAnchoredPosition);
	// Last, once the widget has a parent and therefore a render canvas to be enrolled with.
	NewWidget->CreateNewVisual<UDreamVisualEmpty>();
	// The rule the runtime's own creation roads apply -- UDreamUIBPLibrary's RegisterAndPark and
	// RegisterDreamWidgetHierarchy both begin a widget made after the MANAGER has begun play. Without it
	// a widget made here would sit registered in a begun world without ever having begun, a state no
	// game can reach, and a behaviour added to it later would never Awake.
	if (IsValid(DriverContext->Manager) && DriverContext->Manager->HasBegunPlay() && !NewWidget->HasBegunPlay())
	{
		NewWidget->BeginPlay();
	}
	return NewWidget;
}

UDreamWidget* FDreamDriverRig::MakeControl(TSubclassOf<UDreamUserWidget> InClass, const FString& InDisplayName,
	UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr || !IsValid(InClass))
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : DriverContext->Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}
	// Before the control exists, so nothing it does on the way in -- a part taking the selection, a
	// field beginning an edit -- meets a world without the host a game would have given it.
	EnsureGameInputHost();

	// The runtime's own factory, not a copy of it. What it does in order -- instance, Initialize,
	// parent before register, register the whole hierarchy -- is exactly the part a fixture would get
	// subtly wrong by hand, and the callback is the seam it offers for writing properties before
	// anything registered can observe them.
	UDreamUserWidget* Control = CreateDreamWidget(DriverContext->World, InClass, Parent,
		[&InDisplayName, &InSize](UDreamUserWidget* InBuilt)
		{
			InBuilt->SetDisplayName(InDisplayName);
			InBuilt->SetWidth(InSize.X);
			InBuilt->SetHeight(InSize.Y);
		});
	if (Control == nullptr)
	{
		return nullptr;
	}
	// After registration, as in MakeWidget: the anchor is resolved against the parent the control is
	// now registered under.
	Control->SetAnchoredPosition(InAnchoredPosition);
	return Control;
}

void FDreamDriverRig::EnsureGameInputHost()
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr)
	{
		return;
	}
	UWorld* HostWorld = DriverContext->World;

	UDreamUIManagerWorldSubsystem* HostManager = DriverContext->Manager;
	UDreamEventSystem* HostEventSystem = DriverContext->EventSystem;
	if (IsValid(HostManager) && IsValid(HostEventSystem)
		&& HostManager->GetEventSystemByUserIndex(HostEventSystem->GetUserIndex()) != HostEventSystem)
	{
		// The same call UDreamEventSystem::BeginPlay makes. Not BeginPlay itself: that would also mark
		// the component as having begun play in a world that never did, and nothing here needs the
		// rest of what that means.
		HostManager->AddEventSystem(HostEventSystem);
	}

	APlayerController* HostController = HostWorld->GetFirstPlayerController();
	if (HostController == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		HostController = HostWorld->SpawnActor<APlayerController>(SpawnParameters);
		if (HostController != nullptr)
		{
			// Spawning alone does NOT put it on the world's controller list here. That happens in
			// AController::PostInitializeComponents, and AActor::PostActorConstruction runs
			// Pre/PostInitializeComponents only when World->AreActorsInitialized() -- which a world made
			// with UWorld::CreateWorld never is, because nothing ran InitializeActorsForPlay on it. Off the
			// list, the controller is invisible to GetFirstPlayerController and to
			// UGameplayStatics::GetPlayerController, so the text field's key agent found no player to
			// enable its input on, kept a null InputComponent, and BindKeys dereferenced it. This is the
			// one call the skipped PostInitializeComponents would have made that anything here needs.
			HostWorld->AddController(HostController);
		}
	}
	if (HostController != nullptr && HostController->PlayerInput == nullptr)
	{
		// What a game gives the controller when a local player is assigned to it (SetPlayer): its
		// PlayerInput and its own InputComponent. It also pushes input onto every AutoReceiveInput actor
		// that registered while there was no controller to enable it on (ULevel::PushPendingAutoReceiveInput),
		// which is the other half of how a field's key agent gets its InputComponent. AFTER AddController:
		// that push finds the controller's player index by walking the world's controller list.
		HostController->InitInputSystem();
	}
}

void FDreamDriverRig::PumpFrames(int32 InFrameCount)
{
	if (DriverContext.IsValid())
	{
		DriverContext->PumpFrames(InFrameCount);
	}
}
