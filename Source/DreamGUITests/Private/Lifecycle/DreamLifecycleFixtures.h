// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class ADreamWorldWidgetActor;
class UClass;
class UDreamWidgetBlueprint;
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

	/**
	 * A widget class whose root draws a rect block, compiled into /Temp/DreamGUITests/<InName>. The rect
	 * block draws with a material, so the canvas that draws it makes material instances that read its
	 * data textures.
	 */
	struct FScopedPanelClass
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		explicit FScopedPanelClass(const TCHAR* InName);
		~FScopedPanelClass();
		UE_NONCOPYABLE(FScopedPanelClass);
		UClass* GetClass() const;
	};

	/** A panel of InClass placed in InWorld as a level designer places one, drawn twice so its canvas has made its materials. */
	ADreamWorldWidgetActor* PlacePanel(UWorld* InWorld, UClass* InClass);

	/** The first material instance on InMesh that reads a dynamic texture -- one of a canvas's data textures. */
	UMaterialInstanceDynamic* FindMaterialReadingADynamicTexture(const UMeshComponent* InMesh);

	/** InLines joined for a test message, or "none". */
	FString JoinLines(const TArray<FString>& InLines);
}
