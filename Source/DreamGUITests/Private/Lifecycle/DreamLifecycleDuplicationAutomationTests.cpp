// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/UObjectIterator.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIDataTexture.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DreamUIBPLibrary.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Extensions/DreamPostProcessRenderElement.h"
#include "Extensions/DreamPostProcessRenderElement_Text.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "UMG/DreamUMGWidget.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Lifecycle/DreamLifecycleProbe.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

/*
 * Copy, paste and play in the editor, with a world-space panel in the level.
 *
 * The canvas of a world-space panel draws through a mesh component that belongs to the host actor. The
 * level editor's Copy writes out every object inside a copied actor that is not marked text-export
 * transient, so the mesh went into the text; Paste made it an ordinary component of the new actor --
 * not transient any more -- whose materials still named the materials of the original panel. Starting
 * play then duplicated the level, and a play-in-editor duplication carries every non-transient
 * component of every actor (AActor::Serialize, PPF_DuplicateForPIE) whether or not anything refers to
 * it. Through that pasted mesh's materials it cloned the original panel's canvas and the dynamic
 * textures its materials read: a UTexture2DDynamic keeps its size and mip count outside its
 * properties, so the clone came out zero by zero with no mips, and creating it asserted on the render
 * thread. The level saved before play held none of this -- nothing refers to the pasted mesh, so the
 * save left it out -- which is why the map looked clean.
 *
 * Each test pins one link of that chain. They run headless: the probes find the zero-size textures and
 * the persistent meshes before any RHI is asked to create anything.
 *
 * The last three widen the net to everything else a panel makes while it runs -- its tree in the level
 * editor, the render target a canvas makes for itself, the material instance and body setup of a
 * surface that shows that target -- each of which is one stored reference away from the same trip.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleCopyLeavesTheCanvasMeshOutTest,
	"DreamGUI.Lifecycle.CopyingAWorldWidgetActorLeavesItsCanvasMeshOutOfTheText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleCopyLeavesTheCanvasMeshOutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedPanelClass Panel(TEXT("LifecycleCopyText"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamCanvas* Canvas = Actor->GetWidgetComponent()->GetLoadedCanvas();
	if (!TestNotNull(TEXT("with a canvas"), Canvas))return false;
	UDreamUIMeshComponent* Mesh = Canvas->GetUIMesh();
	if (!TestNotNull(TEXT("that has a mesh"), Mesh))return false;
	TestEqual(TEXT("which belongs to the host actor"), Mesh->GetOwner(), static_cast<AActor*>(Actor));

	FString Copied;
	DreamTests::Lifecycle::CopyPasteActor(Level.World, Actor, 0, &Copied);
	TestTrue(TEXT("Copy wrote the actor out"), Copied.Contains(ADreamWorldWidgetActor::StaticClass()->GetName()));
	TestFalse(TEXT("but not the canvas mesh, which the host's canvas makes for itself"), Copied.Contains(UDreamUIMeshComponent::StaticClass()->GetName()));
	TestFalse(TEXT("nor any of the canvas's data textures"), Copied.Contains(UDreamUIDataTexture::StaticClass()->GetName()) || Copied.Contains(UTexture2DDynamic::StaticClass()->GetName()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecyclePasteLeavesNoPersistentMeshTest,
	"DreamGUI.Lifecycle.PastingAWorldWidgetActorLeavesNoCanvasMeshTheLevelWouldKeep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecyclePasteLeavesNoPersistentMeshTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedPanelClass Panel(TEXT("LifecyclePaste"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	if (!TestNotNull(TEXT("and its canvas drew with a material that reads a data texture"),
		FindMaterialReadingADynamicTexture(Actor->GetWidgetComponent()->GetLoadedCanvas()->GetUIMesh())))return false;

	const TArray<AActor*> Pasted = DreamTests::Lifecycle::CopyPasteActor(Level.World, Actor, 3);
	TestEqual(TEXT("three pastes made three actors"), Pasted.Num(), 3);
	DreamTests::Lifecycle::DrawFrames(Level.World, 2);

	const TArray<FString> Persistent = DreamTests::Lifecycle::FindPersistentCanvasMeshes(Level.World);
	TestEqual(FString::Printf(TEXT("no canvas mesh in the level is one it would save, copy or duplicate for play (found: %s)"), *JoinLines(Persistent)), Persistent.Num(), 0);
	TArray<FString> Bridges;
	for (const DreamUI::FTreeBridge& Bridge : DreamUI::FindTreeBridges(*Level.World))
	{
		Bridges.Add(FString::Printf(TEXT("%s -> %s"), *Bridge.From, *Bridge.To));
	}
	TestEqual(FString::Printf(TEXT("and nothing the level keeps refers into a panel's tree (found: %s)"), *JoinLines(Bridges)), Bridges.Num(), 0);
	for (AActor* PastedActor : Pasted)
	{
		const ADreamWorldWidgetActor* PastedPanel = Cast<ADreamWorldWidgetActor>(PastedActor);
		if (!TestNotNull(TEXT("a pasted actor is a panel"), PastedPanel))continue;
		TestTrue(TEXT("that built its own tree"), IsValid(PastedPanel->GetWidgetComponent()->GetLoadedWidget()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecyclePlayAfterPasteTest,
	"DreamGUI.Lifecycle.APlaySessionStartedAfterAPasteClonesNoWidgetTreeTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecyclePlayAfterPasteTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedPanelClass Panel(TEXT("LifecyclePlayAfterPaste"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	if (!TestNotNull(TEXT("and its canvas drew with a material that reads a data texture"),
		FindMaterialReadingADynamicTexture(Actor->GetWidgetComponent()->GetLoadedCanvas()->GetUIMesh())))return false;

	// Seven pastes, as many as it took the first time.
	DreamTests::Lifecycle::CopyPasteActor(Level.World, Actor, 7);
	DreamTests::Lifecycle::DrawFrames(Level.World, 2);
	TestEqual(FString::Printf(TEXT("before play, no texture in the process is zero-sized (found: %s)"), *JoinLines(DreamTests::Lifecycle::FindZeroSizeDynamicTextures())),
		DreamTests::Lifecycle::FindZeroSizeDynamicTextures().Num(), 0);

	const int32 CopiesBefore = DreamUI::GetCopiedIntoPlaySessionCount();
	UWorld* PlayWorld = DreamTests::Lifecycle::DuplicateWorldForPlayInEditor(Level.World);
	if (!TestNotNull(TEXT("the level was duplicated for play"), PlayWorld))return false;
	const int32 TreeObjectsCopied = DreamUI::GetCopiedIntoPlaySessionCount() - CopiesBefore;
	const TArray<FString> ZeroSize = DreamTests::Lifecycle::FindZeroSizeDynamicTextures();
	const TArray<FString> ClonedTextures = DreamTests::Lifecycle::FindObjectsInPackage(PlayWorld->GetOutermost(), UDreamUIDataTexture::StaticClass());
	const TArray<FString> ClonedCanvases = DreamTests::Lifecycle::FindObjectsInPackage(PlayWorld->GetOutermost(), UDreamCanvas::StaticClass());
	DreamTests::Lifecycle::DestroyDuplicatedWorld(PlayWorld);

	TestEqual(FString::Printf(TEXT("the duplication made no texture the RHI would refuse (found: %s)"), *JoinLines(ZeroSize)), ZeroSize.Num(), 0);
	TestEqual(FString::Printf(TEXT("it cloned none of the editor panels' data textures (found: %s)"), *JoinLines(ClonedTextures)), ClonedTextures.Num(), 0);
	TestEqual(FString::Printf(TEXT("nor any of their canvases: a panel in play builds its own tree (found: %s)"), *JoinLines(ClonedCanvases)), ClonedCanvases.Num(), 0);
	TestEqual(TEXT("nor any other object of a widget tree"), TreeObjectsCopied, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleOrphanMeshNeutralizedTest,
	"DreamGUI.Lifecycle.ACanvasMeshWithoutTransientIsNeutralizedWhenItRegisters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleOrphanMeshNeutralizedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	// The state a paste by an older build left behind, or a map saved by one: a canvas mesh that is an
	// ordinary component of its actor, still naming another panel's material.
	FScopedPanelClass Panel(TEXT("LifecycleOrphan"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Source = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("a panel was placed"), Source))return false;
	UMaterialInstanceDynamic* SourceMaterial = FindMaterialReadingADynamicTexture(Source->GetWidgetComponent()->GetLoadedCanvas()->GetUIMesh());
	if (!TestNotNull(TEXT("and drew with a material that reads a data texture"), SourceMaterial))return false;

	AActor* Holder = Level.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor to hold the stray mesh"), Holder))return false;
	UDreamUIMeshComponent* Stray = NewObject<UDreamUIMeshComponent>(Holder, TEXT("StrayCanvasMesh"), RF_Transactional);
	Stray->OverrideMaterials.Add(SourceMaterial);
	TestFalse(TEXT("the stray mesh starts out as the level would keep it"), Stray->HasAnyFlags(RF_Transient));

	AddExpectedMessagePlain(TEXT("neutralized an orphaned canvas mesh"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	Stray->RegisterComponent();
	TestTrue(TEXT("registering it made it transient"), Stray->HasAnyFlags(RF_Transient));
	TestTrue(TEXT("never to be duplicated for play"), Stray->HasAnyFlags(RF_DuplicateTransient));
	TestTrue(TEXT("nor written out by a copy"), Stray->HasAnyFlags(RF_TextExportTransient));
	TestEqual(TEXT("and it lets go of the other panel's materials"), Stray->OverrideMaterials.Num(), 0);

	// The paste path reaches the same place through PostEditImport.
	UDreamUIMeshComponent* Imported = NewObject<UDreamUIMeshComponent>(Holder, TEXT("ImportedCanvasMesh"), RF_Transactional);
	Imported->OverrideMaterials.Add(SourceMaterial);
	Imported->PostEditImport();
	TestTrue(TEXT("an imported one is made transient too"), Imported->HasAllFlags(RF_Transient | RF_DuplicateTransient | RF_TextExportTransient));
	TestEqual(TEXT("and lets go of the materials as well"), Imported->OverrideMaterials.Num(), 0);

	Stray->DestroyComponent();
	Imported->DestroyComponent();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleDataTexturesStayOutsideTheWorldTest,
	"DreamGUI.Lifecycle.ACanvasKeepsItsDataTexturesOutsideTheWorldAndOutOfEveryCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleDataTexturesStayOutsideTheWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedPanelClass Panel(TEXT("LifecycleDataTextures"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamCanvas* Canvas = Actor->GetWidgetComponent()->GetLoadedCanvas();
	if (!TestNotNull(TEXT("with a canvas"), Canvas))return false;

	const EObjectFlags NeverCopied = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;
	UTexture* ClipTexture = Canvas->GetClipDataTexture();
	UDreamUIDataAsTexture* PropertyData = Canvas->GetWidgetPropertyDataAsTexture();
	if (!TestNotNull(TEXT("the canvas has its clip data texture"), ClipTexture))return false;
	if (!TestNotNull(TEXT("and its widget property data"), PropertyData))return false;
	UTexture* PropertyTexture = PropertyData->GetDataTexture();
	if (!TestNotNull(TEXT("with a texture"), PropertyTexture))return false;

	for (const UObject* Object : TArray<const UObject*>{ ClipTexture, PropertyTexture })
	{
		TestTrue(FString::Printf(TEXT("%s is never saved, duplicated or copied"), *Object->GetName()), Object->HasAllFlags(NeverCopied));
		TestTrue(FString::Printf(TEXT("%s lives in the transient package, outside any world a duplication starts from"), *Object->GetName()), Object->GetOutermost() == GetTransientPackage());
	}
	TestTrue(TEXT("the widget property data object is never saved, duplicated or copied either"), PropertyData->HasAllFlags(NeverCopied));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleForeignMaterialBridgeTest,
	"DreamGUI.Lifecycle.APlaySessionDuplicatingACanvasMaterialLeavesItsDataTextureAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleForeignMaterialBridgeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	// Another way into a panel's tree: a component the level keeps holding one of the canvas's material
	// instances -- a render target shown on a static mesh, or a Blueprint that stored the material.
	FScopedPanelClass Panel(TEXT("LifecycleForeignBridge"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UMaterialInstanceDynamic* CanvasMaterial = FindMaterialReadingADynamicTexture(Actor->GetWidgetComponent()->GetLoadedCanvas()->GetUIMesh());
	if (!TestNotNull(TEXT("and drew with a material that reads a data texture"), CanvasMaterial))return false;

	AActor* Holder = Level.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor for the static mesh"), Holder))return false;
	UStaticMeshComponent* Shown = NewObject<UStaticMeshComponent>(Holder, TEXT("ShowsTheCanvasMaterial"), RF_Transactional);
	Holder->AddInstanceComponent(Shown);
	Shown->RegisterComponent();
	Shown->OverrideMaterials.Add(CanvasMaterial);

	const int32 CopiesBefore = DreamUI::GetCopiedIntoPlaySessionCount();
	UWorld* PlayWorld = DreamTests::Lifecycle::DuplicateWorldForPlayInEditor(Level.World);
	if (!TestNotNull(TEXT("the level was duplicated for play"), PlayWorld))return false;
	const int32 TreeObjectsCopied = DreamUI::GetCopiedIntoPlaySessionCount() - CopiesBefore;
	const TArray<FString> ZeroSize = DreamTests::Lifecycle::FindZeroSizeDynamicTextures();
	const TArray<FString> ClonedTextures = DreamTests::Lifecycle::FindObjectsInPackage(PlayWorld->GetOutermost(), UDreamUIDataTexture::StaticClass());
	DreamTests::Lifecycle::DestroyDuplicatedWorld(PlayWorld);

	TestEqual(FString::Printf(TEXT("the duplication made no texture the RHI would refuse (found: %s)"), *JoinLines(ZeroSize)), ZeroSize.Num(), 0);
	TestEqual(FString::Printf(TEXT("and cloned no data texture: they are not in the world it duplicates (found: %s)"), *JoinLines(ClonedTextures)), ClonedTextures.Num(), 0);
	TestEqual(TEXT("nor any object of the panel's tree"), TreeObjectsCopied, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleEditorTreeLeftOutTest,
	"DreamGUI.Lifecycle.APanelInTheLevelEditorKeepsItsTreeOutOfEveryCopyOfTheLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleEditorTreeLeftOutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	FScopedPanelClass Panel(TEXT("LifecycleEditorTree"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamWidget* Root = Actor->GetWidgetComponent()->GetLoadedWidget();
	if (!TestNotNull(TEXT("with its tree built"), Root))return false;

	const EObjectFlags NeverCopied = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;
	const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Root->GetOuter());
	if (!TestNotNull(TEXT("in a tree object of its own"), Tree))return false;
	TestTrue(TEXT("the tree object is never saved, duplicated or copied"), Tree->HasAllFlags(NeverCopied));

	TArray<UDreamWidget*> Widgets;
	UDreamWidget::CollectChildrenWidgets(Root, Widgets, true);
	TArray<FString> Missing;
	UDreamWidget* Inner = nullptr;
	for (UDreamWidget* Widget : Widgets)
	{
		if (!Widget->HasAllFlags(NeverCopied))
		{
			Missing.Add(Widget->GetPathName());
		}
		if (Inner == nullptr && !Widget->IsA<UDreamUserWidget>())
		{
			Inner = Widget;
		}
	}
	TestEqual(FString::Printf(TEXT("nor is any widget in it: a play session's copy of a reference to one gets null, not a clone of its tree (missing: %s)"), *JoinLines(Missing)), Missing.Num(), 0);

	// A copy made while the level is open -- a list's row, a dropdown's item -- is left out the same way.
	if (!TestNotNull(TEXT("the tree has a widget of the class's own"), Inner))return false;
	UDreamWidget* Copy = UDreamUIBPLibrary::DuplicateWidget(Level.World, Inner, Inner);
	if (!TestNotNull(TEXT("which duplicates"), Copy))return false;
	TestTrue(TEXT("into a widget that is never saved, duplicated or copied either"), Copy->HasAllFlags(NeverCopied));
	Copy->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleRenderTargetSurfaceTest,
	"DreamGUI.Lifecycle.ARenderTargetSurfaceKeepsWhatItMadeOutOfTheCopyAPlaySessionMakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLifecycleRenderTargetSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	// A render-target canvas in the level, shown on a surface: the canvas makes its own target, the
	// surface a material instance that samples it -- when its scene proxy is built, which needs a real
	// RHI, hence NonNullRHI. StaticMesh mode hands that instance to a static mesh
	// component the level keeps, which is what the end of this test does by hand.
	FScopedWorld Level(EWorldType::Editor);
	AActor* Actor = Level.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor for the surface"), Actor))return false;
	USceneComponent* Anchor = NewObject<USceneComponent>(Actor, TEXT("Anchor"), RF_Transactional);
	Actor->SetRootComponent(Anchor);
	Actor->AddInstanceComponent(Anchor);
	Anchor->RegisterComponent();

	UDreamWidget* Root = UDreamUIBPLibrary::ConstructWidget(Level.World, TEXT("SurfaceCanvas"), nullptr);
	UDreamCanvas* Canvas = Root != nullptr ? Root->AddComponent<UDreamCanvas>() : nullptr;
	if (!TestNotNull(TEXT("a canvas was built"), Canvas))
	{
		if (Root != nullptr)
		{
			Root->DestroyWidget();
		}
		return false;
	}
	Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
	UDreamUIBPLibrary::AttachWidgetToSceneComponent(Root, Anchor);
	DreamTests::Lifecycle::DrawFrames(Level.World, 2);

	const EObjectFlags NeverCopied = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;
	UTextureRenderTarget2D* Made = Canvas->GetRenderTarget();
	if (!TestNotNull(TEXT("drawing to a render target made one"), Made))
	{
		Root->DestroyWidget();
		return false;
	}
	TestTrue(TEXT("which is never saved, duplicated or copied"), Made->HasAllFlags(NeverCopied));

	// After the target exists, so the material the surface makes when it registers samples it.
	UDreamUIRenderTargetGeometrySource* Surface = NewObject<UDreamUIRenderTargetGeometrySource>(Actor, TEXT("Surface"), RF_Transactional);
	Surface->SetCanvas(Canvas);
	Surface->SetupAttachment(Anchor);
	Actor->AddInstanceComponent(Surface);
	Surface->RegisterComponent();
	UMaterialInstanceDynamic* Material = Surface->GetMaterialInstance();
	UBodySetup* Body = Surface->GetBodySetup();
	if (TestNotNull(TEXT("the surface made a material instance to show the target"), Material))
	{
		TestTrue(TEXT("which is never saved, duplicated or copied"), Material->HasAllFlags(NeverCopied));
	}
	if (TestNotNull(TEXT("and a body setup for its quad"), Body))
	{
		TestTrue(TEXT("which is never saved, duplicated or copied either"), Body->HasAllFlags(NeverCopied));
	}

	UStaticMeshComponent* Shown = NewObject<UStaticMeshComponent>(Actor, TEXT("ShowsTheSurface"), RF_Transactional);
	Shown->SetupAttachment(Anchor);
	Actor->AddInstanceComponent(Shown);
	Shown->RegisterComponent();
	Shown->SetMaterial(0, Material);

	UWorld* PlayWorld = DreamTests::Lifecycle::DuplicateWorldForPlayInEditor(Level.World);
	if (!TestNotNull(TEXT("the level was duplicated for play"), PlayWorld))
	{
		Root->DestroyWidget();
		return false;
	}
	const UStaticMeshComponent* ShownInPlay = nullptr;
	ForEachObjectWithPackage(PlayWorld->GetOutermost(), [&ShownInPlay](UObject* Object)
	{
		if (Object->GetFName() == FName(TEXT("ShowsTheSurface")) && Object->IsA<UStaticMeshComponent>())
		{
			ShownInPlay = Cast<UStaticMeshComponent>(Object);
			return false;
		}
		return true;
	});
	const bool bShownWasCopied = ShownInPlay != nullptr;
	const UMaterialInterface* SlotInPlay = bShownWasCopied && ShownInPlay->OverrideMaterials.Num() > 0 ? ShownInPlay->OverrideMaterials[0].Get() : nullptr;
	const FString SlotInPlayName = GetPathNameSafe(SlotInPlay);
	const TArray<FString> ClonedMaterials = DreamTests::Lifecycle::FindObjectsInPackage(PlayWorld->GetOutermost(), UMaterialInstanceDynamic::StaticClass());
	const TArray<FString> ClonedTargets = DreamTests::Lifecycle::FindObjectsInPackage(PlayWorld->GetOutermost(), UTextureRenderTarget2D::StaticClass());
	const TArray<FString> ClonedCanvases = DreamTests::Lifecycle::FindObjectsInPackage(PlayWorld->GetOutermost(), UDreamCanvas::StaticClass());
	DreamTests::Lifecycle::DestroyDuplicatedWorld(PlayWorld);

	TestTrue(TEXT("the play session copied the static mesh component"), bShownWasCopied);
	TestNull(FString::Printf(TEXT("but its copy holds no clone of the surface's material: the slot stays empty for the surface in play to fill with its own (holds: %s)"), *SlotInPlayName), SlotInPlay);
	TestEqual(FString::Printf(TEXT("no material instance was cloned into the play world (found: %s)"), *JoinLines(ClonedMaterials)), ClonedMaterials.Num(), 0);
	TestEqual(FString::Printf(TEXT("nor the canvas's render target (found: %s)"), *JoinLines(ClonedTargets)), ClonedTargets.Num(), 0);
	TestEqual(FString::Printf(TEXT("nor, through it, the canvas (found: %s)"), *JoinLines(ClonedCanvases)), ClonedCanvases.Num(), 0);

	// A target handed to the canvas from outside belongs to whoever made it, flags and all.
	UTextureRenderTarget2D* Given = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	Given->InitCustomFormat(64, 64, PF_B8G8R8A8, false);
	Canvas->SetRenderTarget(Given);
	DreamTests::Lifecycle::DrawFrames(Level.World, 1);
	TestTrue(TEXT("a render target handed to the canvas is the one it draws to"), Canvas->GetRenderTarget() == Given);
	TestFalse(TEXT("and the canvas leaves its flags alone"), Given->HasAnyFlags(RF_DuplicateTransient | RF_TextExportTransient));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleTreeBridgeReportedTest,
	"DreamGUI.Lifecycle.AReferenceIntoAPanelsTreeIsListedAndWhatAPlaySessionCopiesThroughItIsCounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleTreeBridgeReportedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests::Lifecycle;

	// A reference no flag of DreamGUI's can cut: a material instance someone else made inside the
	// panel's tree, without the flags, kept by a static mesh component of another actor.
	FScopedPanelClass Panel(TEXT("LifecycleTreeBridge"));
	if (!TestNotNull(TEXT("the panel class compiled"), Panel.GetClass()))return false;
	FScopedWorld Level(EWorldType::Editor);
	ADreamWorldWidgetActor* Actor = PlacePanel(Level.World, Panel.GetClass());
	if (!TestNotNull(TEXT("the panel was placed"), Actor))return false;
	UDreamCanvas* Canvas = Actor->GetWidgetComponent()->GetLoadedCanvas();
	if (!TestNotNull(TEXT("with a canvas"), Canvas))return false;
	UMaterialInstanceDynamic* Foreign = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Canvas, TEXT("MadeInsideTheTree"));
	if (!TestNotNull(TEXT("a material instance made inside the tree"), Foreign))return false;

	AActor* Holder = Level.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor for the static mesh"), Holder))return false;
	UStaticMeshComponent* Shown = NewObject<UStaticMeshComponent>(Holder, TEXT("HoldsATreeMaterial"), RF_Transactional);
	Holder->AddInstanceComponent(Shown);
	Shown->RegisterComponent();
	Shown->SetMaterial(0, Foreign);

	const TArray<DreamUI::FTreeBridge> Bridges = DreamUI::FindTreeBridges(*Level.World);
	if (TestEqual(TEXT("the one reference into a tree is listed"), Bridges.Num(), 1))
	{
		TestEqual(TEXT("from the component that holds it"), Bridges[0].From, Shown->GetPathName());
		TestEqual(TEXT("to the material instance"), Bridges[0].To, Foreign->GetPathName());
		TestEqual(TEXT("in the tree of the panel's actor"), Bridges[0].Host, Actor->GetPathName());
		TestFalse(TEXT("which is not the holder's own"), Bridges[0].bIntoOwnHost);
	}

	// Following it, a play session's copy of the level clones the canvas and its outers: each is
	// reported and counted where it is copied. The report ensures outside a test; this one expects it.
	AddExpectedMessagePlain(TEXT("was copied into a play session's world"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	const int32 CopiesBefore = DreamUI::GetCopiedIntoPlaySessionCount();
	int32 TreeObjectsCopied = 0;
	TArray<FString> ClonedCanvases;
	{
		DreamUI::FScopedExpectedCopiesIntoPlaySession Expected;
		UWorld* PlayWorld = DreamTests::Lifecycle::DuplicateWorldForPlayInEditor(Level.World);
		if (!TestNotNull(TEXT("the level was duplicated for play"), PlayWorld))return false;
		TreeObjectsCopied = DreamUI::GetCopiedIntoPlaySessionCount() - CopiesBefore;
		ClonedCanvases = DreamTests::Lifecycle::FindObjectsInPackage(PlayWorld->GetOutermost(), UDreamCanvas::StaticClass());
		DreamTests::Lifecycle::DestroyDuplicatedWorld(PlayWorld);
	}
	TestEqual(TEXT("the copy followed the reference to the canvas"), ClonedCanvases.Num(), 1);
	TestTrue(FString::Printf(TEXT("and every tree object it copied was counted (%d)"), TreeObjectsCopied), TreeObjectsCopied >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleRuntimePropertiesLeftOutTest,
	"DreamGUI.Lifecycle.EveryPropertyThatHoldsARuntimeObjectIsLeftOutOfSavesDuplicatesAndCopies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleRuntimePropertiesLeftOutTest::RunTest(const FString& Parameters)
{
	// The object flags keep a runtime object out of a copy of the world; the property flags keep the
	// reference to it out of a copy of its owner, so the copy makes its own instead of sharing, or
	// cloning, the original's. Render targets a user may assign (the canvas's, the post process's
	// output) are not here: those properties are the user's.
	struct FHolder
	{
		const UClass* Class;
		const TCHAR* Property;
	};
	const FHolder Holders[] =
	{
		{ UDreamCanvas::StaticClass(), TEXT("UIMesh") },
		{ UDreamCanvas::StaticClass(), TEXT("ClipDataAsTexture") },
		{ UDreamCanvas::StaticClass(), TEXT("WidgetPropertyDataAsTexture") },
		{ UDreamUIDataAsTexture::StaticClass(), TEXT("Texture") },
		{ UDreamRectBlock::StaticClass(), TEXT("DataRows") },
		{ UDreamUIManagerWorldSubsystem::StaticClass(), TEXT("RectBlockDataRows") },
		{ UDreamUIRenderTargetGeometrySource::StaticClass(), TEXT("BodySetup") },
		{ UDreamUIRenderTargetGeometrySource::StaticClass(), TEXT("MaterialInstance") },
		{ UDreamUMGWidget::StaticClass(), TEXT("RenderTarget") },
		{ UDreamPostProcessRenderElement::StaticClass(), TEXT("MaterialInstanceDynamic") },
		{ UDreamPostProcessRenderElement_Text::StaticClass(), TEXT("MaterialInstanceDynamic") },
	};
	constexpr EPropertyFlags LeftOut = CPF_Transient | CPF_DuplicateTransient | CPF_TextExportTransient;
	for (const FHolder& Holder : Holders)
	{
		const FString Name = FString::Printf(TEXT("%s.%s"), *Holder.Class->GetName(), Holder.Property);
		const FProperty* Property = FindFProperty<FProperty>(Holder.Class, Holder.Property);
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), *Name), Property))continue;
		TestTrue(FString::Printf(TEXT("%s is left out of saves, duplicates and copies"), *Name), Property->HasAllPropertyFlags(LeftOut));
	}
	return true;
}

namespace DreamLifecycleDuplicationAuditLocal
{
	/** What an owner makes for itself at run time, and a copy of the owner must not share or clone. */
	bool IsMadeByItsOwner(const UClass* InClass)
	{
		const UClass* const Made[] = {
			UTexture2DDynamic::StaticClass(), UDreamUIDataTexture::StaticClass(), UTextureRenderTarget2D::StaticClass(), UMaterialInstanceDynamic::StaticClass(),
			UDreamUIMeshComponent::StaticClass(), UDreamUIDataAsTexture::StaticClass(), UDreamWidgetTree::StaticClass(),
		};
		for (const UClass* Class : Made)
		{
			if (InClass != nullptr && InClass->IsChildOf(Class))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * The properties that name something an author or the project chose rather than something the owner made:
	 * a render target to draw into, an asset's own authoring tree, and the project's rect block data, an
	 * asset every rect block shares.
	 */
	bool IsAssignedByAnAuthor(const FString& InPath)
	{
		static const TCHAR* const Assigned[] = {
			TEXT("DreamCanvas.RenderTarget"),
			TEXT("DreamVisualPostProcess.OutputRenderTarget"),
			TEXT("DreamWidgetBlueprint.WidgetTree"),
			TEXT("DreamRectBlock.RectBlockData"),
		};
		for (const TCHAR* Path : Assigned)
		{
			if (InPath == Path)
			{
				return true;
			}
		}
		return false;
	}

	bool IsDreamGUIClass(const UClass* InClass)
	{
		if (InClass->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			return false;
		}
		const FString Package = InClass->GetOutermost()->GetName();
		return (Package.StartsWith(TEXT("/Script/DreamGUI")) && Package != TEXT("/Script/DreamGUITests"))
			|| Package == TEXT("/Script/DreamTween");
	}

	/** Every place under InProperty that can hold an object its owner made and is not left out of a copy. */
	void Sweep(const FProperty* InProperty, const FString& InPath, TArray<FString>& OutFound, TArray<const UStruct*>& InStructs)
	{
		if (InProperty->HasAnyPropertyFlags(CPF_DuplicateTransient))
		{
			return;// and everything it holds with it
		}
		if (InProperty->IsA<FClassProperty>() || InProperty->IsA<FSoftObjectProperty>() || InProperty->IsA<FLazyObjectProperty>())
		{
			return;// a class, or a path to an asset: never an object the owner made
		}
		if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(InProperty))
		{
			if (IsMadeByItsOwner(Object->PropertyClass))
			{
				OutFound.Add(InPath);
			}
			return;
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(InProperty))
		{
			Sweep(Array->Inner, InPath + TEXT("[]"), OutFound, InStructs);
			return;
		}
		if (const FSetProperty* Set = CastField<FSetProperty>(InProperty))
		{
			Sweep(Set->ElementProp, InPath + TEXT("{}"), OutFound, InStructs);
			return;
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(InProperty))
		{
			Sweep(Map->KeyProp, InPath + TEXT("{key}"), OutFound, InStructs);
			Sweep(Map->ValueProp, InPath + TEXT("{value}"), OutFound, InStructs);
			return;
		}
		if (const FStructProperty* Struct = CastField<FStructProperty>(InProperty))
		{
			if (InStructs.Contains(Struct->Struct))
			{
				return;
			}
			InStructs.Push(Struct->Struct);
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				Sweep(*It, InPath + TEXT(".") + It->GetName(), OutFound, InStructs);
			}
			InStructs.Pop();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleDuplicationAuditTest,
	"DreamGUI.Lifecycle.NoPropertyOfADreamGUIClassCarriesWhatItsOwnerMadeIntoACopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The list above names the properties known today; this sweeps every property of every DreamGUI class by
 * reflection, into structs and containers, so one added tomorrow is held to the same rule. A texture an owner
 * draws with, a render target it renders into, a material instance it made, its canvas mesh, its data
 * textures, a widget tree it hosts: a copy of the owner -- a save, a play session's duplicate, Copy and Paste --
 * that kept the reference drew with the original's, or with a clone that had no size. Any property that can
 * hold one is DuplicateTransient, unless it is one an author assigns.
 */
bool FDreamLifecycleDuplicationAuditTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleDuplicationAuditLocal;
	TArray<FString> Found;
	int32 Swept = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		const UClass* Class = *It;
		if (!IsDreamGUIClass(Class))
		{
			continue;
		}
		++Swept;
		for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
		{
			const FString Path = FString::Printf(TEXT("%s.%s"), *Class->GetName(), *PropertyIt->GetName());
			if (IsAssignedByAnAuthor(Path))
			{
				continue;
			}
			TArray<const UStruct*> Structs;
			Sweep(*PropertyIt, Path, Found, Structs);
		}
	}
	TestTrue(FString::Printf(TEXT("the sweep reached DreamGUI's classes (%d)"), Swept), Swept > 100);
	TestEqual(FString::Printf(TEXT("no property carries what its owner made into a copy (found: %s)"),
		*DreamTests::Lifecycle::JoinLines(Found)), Found.Num(), 0);
	return true;
}

#endif
