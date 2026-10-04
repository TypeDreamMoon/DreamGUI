// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Misc/PackageName.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * A LEVEL CHANGE IN A PLAY SESSION.
 *
 * A game changes level with OpenLevel: the world it plays is torn down whole and another one loaded in its place, with
 * the same game instance, viewport and player. Everything DreamGUI made for the world it left -- the UI manager, the
 * canvases and their meshes, the render roots and the renderer, the paint rows, every registered widget -- has to come
 * down with that world and be collected, and the world it arrives in has to build all of it again from nothing: a manager
 * of its own with its own paint rows, a screen whose canvas registers with it and draws, a painted text holding a table
 * in the new rows. Nothing may reach the collector still registered, and the engine's own check for objects that keep a
 * world it left alive is part of the trip.
 *
 * The session plays the engine's Entry level, travels to the default template level and back to Entry -- two engine
 * maps, so the trip needs no map of its own on disk -- building the same screen on each.
 */
namespace DreamLifecycleTravelTestLocal
{
	using namespace DreamTests::Lifecycle;

	const TCHAR* const FirstMap = TEXT("/Engine/Maps/Entry");
	const TCHAR* const SecondMap = TEXT("/Engine/Maps/Templates/Template_Default");

	/** What a world's DreamGUI was as the session left it, kept weakly: the rig forbids anything stronger across a travel. */
	struct FWorldSnapshot
	{
		FString MapName;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UDreamUIManagerWorldSubsystem> Manager;
		TArray<TWeakObjectPtr<UDreamCanvas>> Canvases;
		TArray<TWeakObjectPtr<UDreamUIMeshComponent>> Meshes;
		TArray<TWeakObjectPtr<UDreamWidget>> Widgets;
		TWeakObjectPtr<UTexture> PaintRows;
		TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> Renderer;
		/** The meshes that had a render root. */
		int32 RenderRoots = 0;
	};

	/** One world snapshot per travel: the world each travel left. */
	struct FTravelState
	{
		TArray<FWorldSnapshot> Left;
	};

	/** The canvas meshes in InWorld. */
	TArray<UDreamUIMeshComponent*> GetCanvasMeshesOf(const UWorld* InWorld)
	{
		TArray<UObject*> Objects;
		GetObjectsOfClass(UDreamUIMeshComponent::StaticClass(), Objects, true, RF_ClassDefaultObject | RF_ArchetypeObject, EInternalObjectFlags::Garbage);
		TArray<UDreamUIMeshComponent*> Meshes;
		for (UObject* Object : Objects)
		{
			UDreamUIMeshComponent* Mesh = Cast<UDreamUIMeshComponent>(Object);
			if (IsValid(Mesh) && InWorld != nullptr && Mesh->GetWorld() == InWorld)
			{
				Meshes.Add(Mesh);
			}
		}
		return Meshes;
	}

	/** The UI managers alive in play-in-editor worlds. */
	int32 CountPlayWorldManagers()
	{
		TArray<UObject*> Objects;
		GetObjectsOfClass(UDreamUIManagerWorldSubsystem::StaticClass(), Objects, true, RF_ClassDefaultObject | RF_ArchetypeObject, EInternalObjectFlags::Garbage);
		int32 Count = 0;
		for (UObject* Object : Objects)
		{
			const UWorld* World = Object != nullptr ? Object->GetTypedOuter<UWorld>() : nullptr;
			Count += World != nullptr && World->WorldType == EWorldType::PIE ? 1 : 0;
		}
		return Count;
	}

	/** The screen every map gets: a button and a text painted with a gradient, on the rig's screen-space root. */
	void BuildScreen(FDreamDriverPieRig& InRig)
	{
		InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0), FVector2D(0.0, -80.0));
		UDreamWidget* Title = InRig.MakeWidget(TEXT("Title"), nullptr, FVector2D(480.0, 60.0), FVector2D(0.0, 80.0));
		UDreamText* Text = Title != nullptr ? Title->CreateNewVisual<UDreamText>() : nullptr;
		if (Text == nullptr)
		{
			InRig.GetTest().AddError(TEXT("The title's text was not made."));
			return;
		}
		Text->SetFontSize(40.0f);
		Text->SetText(FText::FromString(TEXT("Travelling")));
		FDreamTextPaint Paint;
		Paint.bEnabled = true;
		Paint.Gradient.Stops = { FDreamGradientStop(0.0f, FColor(255, 243, 176)), FDreamGradientStop(1.0f, FColor(156, 106, 18)) };
		Text->SetFacePaint(Paint);
	}

	FWorldSnapshot TakeSnapshot(UWorld& InWorld)
	{
		FWorldSnapshot Snapshot;
		Snapshot.MapName = UWorld::RemovePIEPrefix(InWorld.GetOutermost()->GetName());
		Snapshot.World = &InWorld;
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(&InWorld))
		{
			Snapshot.Manager = Manager;
			for (const TWeakObjectPtr<UDreamCanvas>& Canvas : Manager->GetAllCanvasArray())
			{
				if (Canvas.IsValid())
				{
					Snapshot.Canvases.Add(Canvas);
				}
			}
			for (UDreamWidget* Widget : Manager->GetRegisteredWidgets())
			{
				Snapshot.Widgets.Add(Widget);
			}
			Snapshot.PaintRows = Manager->GetPaintRowsTexture();
			Snapshot.Renderer = UDreamUIManagerWorldSubsystem::GetViewExtension(&InWorld, /*InCreateIfNotExist*/ false);
		}
		for (UDreamUIMeshComponent* Mesh : GetCanvasMeshesOf(&InWorld))
		{
			Snapshot.Meshes.Add(Mesh);
			Snapshot.RenderRoots += Mesh->GetRenderRoot() != nullptr ? 1 : 0;
		}
		return Snapshot;
	}

	/** What a level change leaves for the collector, collected; and the render thread through what was sent before it. */
	void Settle()
	{
		FlushRenderingCommands();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		FlushRenderingCommands();
	}

	template<typename T>
	void ExpectCollected(FAutomationTestBase& InTest, const FString& InWhat, const TArray<TWeakObjectPtr<T>>& InObjects)
	{
		int32 Left = 0;
		for (const TWeakObjectPtr<T>& Object : InObjects)
		{
			Left += Object.IsValid(/*bEvenIfGarbage*/ true) ? 1 : 0;
		}
		InTest.TestEqual(FString::Printf(TEXT("%s: all %d collected"), *InWhat, InObjects.Num()), Left, 0);
	}

	/** Everything InOld held of the world it was taken in is gone. */
	void ExpectLeftBehindIsGone(FAutomationTestBase& InTest, const FString& InWhen, const FWorldSnapshot& InOld)
	{
		const FString Left = FString::Printf(TEXT("%s, what %s held"), *InWhen, *InOld.MapName);
		InTest.TestTrue(Left + TEXT(": the screen it showed had canvases and widgets to lose"), InOld.Canvases.Num() > 0 && InOld.Widgets.Num() > 0);
		InTest.TestFalse(Left + TEXT(": the world is collected"), InOld.World.IsValid(/*bEvenIfGarbage*/ true));
		InTest.TestFalse(Left + TEXT(": its UI manager is collected"), InOld.Manager.IsValid(/*bEvenIfGarbage*/ true));
		ExpectCollected(InTest, Left + TEXT(": its canvases"), InOld.Canvases);
		ExpectCollected(InTest, Left + TEXT(": the canvas meshes, and the render roots they held"), InOld.Meshes);
		ExpectCollected(InTest, Left + TEXT(": its registered widgets"), InOld.Widgets);
		InTest.TestFalse(Left + TEXT(": its paint rows"), InOld.PaintRows.IsValid(/*bEvenIfGarbage*/ true));
		InTest.TestFalse(Left + TEXT(": its renderer, once the render thread ran what it was sent"), InOld.Renderer.IsValid());
	}

	/** The world the session is in now has DreamGUI up, its own and nobody else's. */
	void ExpectBuiltAgain(FAutomationTestBase& InTest, const FString& InWhen, FDreamDriverContext& InContext, const FWorldSnapshot* InOld)
	{
		UWorld* World = InContext.World;
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
		if (!InTest.TestNotNull(InWhen + TEXT(": the world has a UI manager"), Manager))
		{
			return;
		}
		InTest.TestTrue(InWhen + TEXT(": ...that is up and has begun play"), Manager->IsInitialized() && !Manager->HasTornDownWorld() && Manager->HasBegunPlay());
		InTest.TestTrue(InWhen + TEXT(": ...the one the rig's context names"), InContext.Manager == Manager);
		InTest.TestNotNull(InWhen + TEXT(": ...with paint rows of its own"), Manager->GetPaintRowsTexture());
		if (InOld != nullptr)
		{
			InTest.TestTrue(InWhen + TEXT(": ...in another world than the one left"), InOld->World.Get(/*bEvenIfGarbage*/ true) != World);
		}
		InTest.TestEqual(InWhen + TEXT(": one play world's UI manager is alive, this one"), CountPlayWorldManagers(), 1);

		InTest.TestTrue(InWhen + TEXT(": the rig's screen canvas is registered with it"),
			InContext.RootCanvas != nullptr && Manager->IsCanvasStillRegistered(InContext.RootCanvas));
		int32 Canvases = 0;
		int32 ForeignCanvases = 0;
		for (const TWeakObjectPtr<UDreamCanvas>& Canvas : Manager->GetAllCanvasArray())
		{
			if (const UDreamCanvas* Live = Canvas.Get())
			{
				++Canvases;
				ForeignCanvases += Live->GetWorld() != World ? 1 : 0;
			}
		}
		InTest.TestTrue(InWhen + TEXT(": it has canvases registered"), Canvases > 0);
		InTest.TestEqual(InWhen + TEXT(": every one of them is this world's"), ForeignCanvases, 0);
		int32 ForeignWidgets = 0;
		for (const UDreamWidget* Widget : Manager->GetRegisteredWidgets())
		{
			ForeignWidgets += Widget->GetWorld() != World ? 1 : 0;
		}
		InTest.TestEqual(InWhen + TEXT(": every widget registered with it is this world's"), ForeignWidgets, 0);

		UDreamWidget* Title = InContext.Root != nullptr ? InContext.Root->FindChildByDisplayName(TEXT("Title"), /*IncludeChildren*/ true) : nullptr;
		const UDreamText* Text = Title != nullptr ? Cast<UDreamText>(Title->GetVisual()) : nullptr;
		InTest.TestTrue(InWhen + TEXT(": the painted title holds a text table in this world's paint rows"), Text != nullptr && Text->GetPaintTextRow() != INDEX_NONE);
		int32 TextureRows = 0;
		int32 TextRows = 0;
		int32 GradientRows = 0;
		int64 TextureBytes = 0;
		Manager->GetPaintRowsMemoryInfo(TextureRows, TextRows, GradientRows, TextureBytes);
		InTest.TestTrue(InWhen + TEXT(": ...which count its table and its gradient's row"), TextRows >= 1 && GradientRows >= 1);

		if (InOld != nullptr && InOld->RenderRoots > 0)
		{
			int32 RenderRoots = 0;
			for (UDreamUIMeshComponent* Mesh : GetCanvasMeshesOf(World))
			{
				RenderRoots += Mesh->GetRenderRoot() != nullptr ? 1 : 0;
			}
			InTest.TestTrue(FString::Printf(TEXT("%s: the screen's meshes made render roots again (%d before the travel, %d now)"), *InWhen, InOld->RenderRoots, RenderRoots),
				RenderRoots > 0);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleTravelPieTest,
	"DreamGUI.Pie.OpeningAnotherLevelAndComingBackTearsTheUIDownAndBuildsItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleTravelPieTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleTravelTestLocal;
	FDreamPieRigOptions Options;
	Options.MapOverride = FString(FirstMap) + TEXT(".") + FPackageName::GetShortName(FirstMap);
	const TSharedRef<FLeakLogWatch> Watch = MakeShared<FLeakLogWatch>();
	const TSharedRef<FTravelState> State = MakeShared<FTravelState>();
	FAutomationTestBase* Test = this;

	const TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this, Options);
	const TWeakPtr<FDreamDriverPieRig> WeakRig = Rig;
	Rig->Start();
	Rig->WhenReady([](FDreamDriverPieRig& InRig) { BuildScreen(InRig); });
	Rig->Sequence().WaitFrames(2).Then([Test](FDreamDriverContext& InContext)
	{
		ExpectBuiltAgain(*Test, TEXT("On the first map"), InContext, nullptr);
	}).PerformLatent();

	// To the second map, and back to the first.
	const TCHAR* const Destinations[] = { SecondMap, FirstMap };
	const int32 LegCount = UE_ARRAY_COUNT(Destinations);
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		const FString Destination = Destinations[Leg];
		Rig->Travel(Destination, [State](UWorld& InWorld)
		{
			State->Left.Add(TakeSnapshot(InWorld));
		});
		Rig->WhenReady([](FDreamDriverPieRig& InRig) { BuildScreen(InRig); });
		Rig->Sequence().WaitFrames(2).Then([Test, State, WeakRig, Leg, Destination](FDreamDriverContext& InContext)
		{
			const FString When = FString::Printf(TEXT("After travel %d, to %s"), Leg + 1, *Destination);
			if (const TSharedPtr<FDreamDriverPieRig> PinnedRig = WeakRig.Pin())
			{
				Test->TestEqual(When + TEXT(": the rig counts the travel"), PinnedRig->GetTravelCount(), Leg + 1);
			}
			Test->TestEqual(When + TEXT(": the session plays the map it travelled to"),
				UWorld::RemovePIEPrefix(InContext.World->GetOutermost()->GetName()), Destination);
			Settle();
			if (Test->TestTrue(When + TEXT(": the world it left was looked at before it went"), State->Left.IsValidIndex(Leg)))
			{
				ExpectLeftBehindIsGone(*Test, When, State->Left[Leg]);
				ExpectBuiltAgain(*Test, When, InContext, &State->Left[Leg]);
			}
			Test->TestEqual(When + TEXT(": no dynamic texture without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));
		}).PerformLatent();
	}

	Rig->Sequence().Then([Test, Watch](FDreamDriverContext& InContext)
	{
		Test->TestEqual(TEXT("Nothing reached the collector still registered, on any of the maps"), Watch->Describe(), FString(TEXT("none")));
	}).PerformLatent();
	Rig->Finish();
	return true;
}

#endif
