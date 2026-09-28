// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

/*
 * A LEVEL COPIED WITH EVERY KIND OF PANEL IN IT, UNDER THE RENDERER.
 *
 * The direct copies of a level: the whole world duplicated for play, as a play session begins, and duplicated as an
 * asset, as the content browser's Duplicate does -- each with one of every world-space kind in the level: a panel
 * drawn by DreamUI's renderer with a background blur in its tree, one drawn by the engine's, and a render-target
 * surface a static mesh shows as well. The render thread runs everything a copy queued before anything is asked, so
 * a copy that cloned a texture without its size asserts in the step that made it. After each copy nothing was counted
 * into a play session's copy, no dynamic texture anywhere is without a size, and the copy holds nothing of a tree:
 * no widget, tree, canvas mesh or data texture.
 */
namespace DreamLifecycleLevelCopyTestLocal
{
	using namespace DreamTests::Lifecycle;

	/** Everything of a tree that InPackage holds: what a copy of the level must not have taken with it. */
	TArray<FString> FindTreeObjects(const UPackage* InPackage)
	{
		TArray<FString> Found;
		for (const UClass* Class : { UDreamWidget::StaticClass(), UDreamWidgetTree::StaticClass(), UDreamUIMeshComponent::StaticClass(),
				 UDreamUIDataAsTexture::StaticClass(), UTexture2DDynamic::StaticClass() })
		{
			Found.Append(FindObjectsInPackage(InPackage, Class));
		}
		return Found;
	}

	void Settle()
	{
		FlushRenderingCommands();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		FlushRenderingCommands();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleLevelCopyRhiTest,
	"DreamGUI.Lifecycle.RHI.CopyingALevelWithEveryKindOfPanelInItClonesNothingOfTheirTrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleLevelCopyRhiTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleLevelCopyTestLocal;

	FScopedPanelClass Panel(TEXT("LevelCopyRhi"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))
	{
		return false;
	}
	Settle();
	FLeakLogWatch Watch;
	{
		FScopedWorld Level(EWorldType::Editor);
		const FWorldSpaceKinds Kinds = PlaceEveryWorldSpaceKind(Level.World, Panel.GetClass());
		if (!TestEqual(TEXT("One of every world-space kind was placed"), Kinds.Actors().Num(), 3))
		{
			return false;
		}
		TestTrue(TEXT("...the surface showing its canvas"), ShowsItsCanvas(Kinds.Surface));
		FlushRenderingCommands();
		const int32 CopiesBefore = DreamUI::GetCopiedIntoPlaySessionCount();

		// For play, as a session begins.
		UWorld* PlayCopy = DuplicateWorldForPlayInEditor(Level.World);
		FlushRenderingCommands();
		if (TestNotNull(TEXT("The level was duplicated for play"), PlayCopy))
		{
			TestEqual(TEXT("...counting nothing into the play session's copy"), DreamUI::GetCopiedIntoPlaySessionCount() - CopiesBefore, 0);
			TestEqual(TEXT("...and the copy holds nothing of a tree"), JoinLines(FindTreeObjects(PlayCopy->GetOutermost())), FString(TEXT("none")));
			TestEqual(TEXT("...nor a canvas mesh its level would keep"), JoinLines(FindPersistentCanvasMeshes(PlayCopy)), FString(TEXT("none")));
			DestroyDuplicatedWorld(PlayCopy);
		}
		TestEqual(TEXT("...and no dynamic texture anywhere is without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));

		// As an asset, as the content browser's Duplicate does.
		UPackage* AssetPackage = CreatePackage(TEXT("/Temp/DreamGUITests/LevelCopyRhiDuplicate"));
		UWorld* AssetCopy = Cast<UWorld>(StaticDuplicateObject(Level.World, AssetPackage, Level.World->GetFName()));
		FlushRenderingCommands();
		if (TestNotNull(TEXT("The level was duplicated as an asset"), AssetCopy))
		{
			TestEqual(TEXT("...and the copy holds nothing of a tree"), JoinLines(FindTreeObjects(AssetPackage)), FString(TEXT("none")));
			TestEqual(TEXT("...nor a canvas mesh its level would keep"), JoinLines(FindPersistentCanvasMeshes(AssetCopy)), FString(TEXT("none")));
			DestroyDuplicatedWorld(AssetCopy);
		}
		TestEqual(TEXT("...and no dynamic texture anywhere is without a size"), JoinLines(FindZeroSizeDynamicTextures()), FString(TEXT("none")));
		Settle();
		TestTrue(TEXT("The panels drew on through both copies"), ShowsItsCanvas(Kinds.Surface) && Kinds.DreamRendered->GetWidgetComponent()->GetLoadedWidget() != nullptr);
	}
	Settle();
	TestEqual(TEXT("Nothing reached the collector still registered, the world's teardown included"), Watch.Describe(), FString(TEXT("none")));
	return true;
}

#endif
