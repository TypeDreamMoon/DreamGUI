// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class AActor;
class ADreamWorldWidgetActor;
class UClass;
class UDreamUIRenderTargetGeometrySource;
class UDreamWidget;
class UDreamWidgetBlueprint;
class ULevel;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UPackage;
class UWorld;

/*
 * What the lifecycle tests are set in: a world of their own, and a panel in it as a level designer
 * places one.
 */
namespace DreamTests::Lifecycle
{
	/** A world of the given type for the length of a scope. Its widget trees are destroyed with it. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		explicit FScopedWorld(EWorldType::Type InWorldType);
		~FScopedWorld();
		UE_NONCOPYABLE(FScopedWorld);
	};

	/** The console variable InName at InValue for the length of a scope, and back to what it was after. */
	struct FScopedConsoleVariable
	{
		FScopedConsoleVariable(const TCHAR* InName, int32 InValue);
		~FScopedConsoleVariable();
		UE_NONCOPYABLE(FScopedConsoleVariable);
	private:
		class IConsoleVariable* Variable = nullptr;
		int32 Before = 0;
	};

	/**
	 * A widget class whose root draws a rect block, compiled into /Temp/DreamGUITests/<InName>. The rect
	 * block draws with a material, whose parameters -- the canvas's data textures among them -- the canvas
	 * answers through a proxy of it. With bInSavedToDisk the class is written to its file as well, as a class a saved level
	 * refers to is -- a level read back by level streaming finds only what is on disk -- and the file is
	 * removed again with the class.
	 */
	struct FScopedPanelClass
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FString FileName;
		explicit FScopedPanelClass(const TCHAR* InName, bool bInSavedToDisk = false);
		~FScopedPanelClass();
		UE_NONCOPYABLE(FScopedPanelClass);
		UClass* GetClass() const;
	};

	/** The registered hierarchy roots of InWorld. */
	TArray<UDreamWidget*> RegisteredRoots(UWorld* InWorld);

	/** The object a root is held by: the outer of the tree it roots, or its own outer. */
	const UObject* HolderOf(const UDreamWidget* InRoot);

	/** A panel of InClass placed in InWorld as a level designer places one, drawn twice so its canvas has made its materials. */
	ADreamWorldWidgetActor* PlacePanel(UWorld* InWorld, UClass* InClass);

	/** A background blur under InParent with a canvas of its own under it, as a tree holds one. Null when none was made. */
	UDreamWidget* AddBackgroundBlur(UWorld* InWorld, UDreamWidget* InParent);

	/**
	 * A render-target surface put where a level's script puts one at run time: an actor in InLevel (the current
	 * level when null) whose surface shows a canvas the world holds -- a free widget drawing to a render target,
	 * hung on the actor. With bInAlsoOnAStaticMesh a static mesh the actor keeps shows the surface's material too,
	 * as the surface's static mesh mode hands it over.
	 */
	UDreamUIRenderTargetGeometrySource* PlaceSurface(UWorld* InWorld, ULevel* InLevel, UDreamWidget*& OutCanvasRoot, bool bInAlsoOnAStaticMesh = false);

	/**
	 * Whether InSurface shows its canvas: registered, with the canvas's render target -- and, where there is a
	 * renderer, its own material instance showing it, which the surface makes with its scene proxy.
	 */
	bool ShowsItsCanvas(const UDreamUIRenderTargetGeometrySource* InSurface);

	/**
	 * One of each world-space kind, placed as a level designer places them and drawn: a panel drawn by DreamUI's
	 * renderer with a background blur in its tree, a panel drawn by the engine's, and -- with bInWithSurface -- a
	 * render-target surface whose material a static mesh shows as well.
	 */
	struct FWorldSpaceKinds
	{
		ADreamWorldWidgetActor* DreamRendered = nullptr;
		ADreamWorldWidgetActor* EngineRendered = nullptr;
		UDreamUIRenderTargetGeometrySource* Surface = nullptr;
		UDreamWidget* SurfaceCanvasRoot = nullptr;
		/** The actors placed. */
		TArray<AActor*> Actors() const;
	};
	FWorldSpaceKinds PlaceEveryWorldSpaceKind(UWorld* InWorld, UClass* InPanelClass, bool bInWithSurface = true);

	/** The first material instance on InMesh that reads a dynamic texture -- one of a canvas's data textures. */
	UMaterialInstanceDynamic* FindMaterialReadingADynamicTexture(const UMeshComponent* InMesh);

	/** The first material InMesh draws with, or null when it draws with none. */
	UMaterialInterface* FindFirstMaterial(const UMeshComponent* InMesh);

	/** InLines joined for a test message, or "none". */
	FString JoinLines(const TArray<FString>& InLines);
}
