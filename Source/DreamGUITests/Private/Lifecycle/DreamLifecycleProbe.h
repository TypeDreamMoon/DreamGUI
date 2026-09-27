// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UClass;
class UPackage;
class UWorld;

/*
 * What the lifecycle tests look at after every step, and the editor operations they drive.
 *
 * The probes answer questions about the whole process rather than about one widget, because the
 * failures they exist for are never where the step happened: a mesh that a copy carried into another
 * actor, a texture that a world duplication cloned without its size. Each returns the offending
 * objects' paths so a failing test can say which ones.
 */
namespace DreamTests::Lifecycle
{
	/** Every UTexture2DDynamic whose width, height or mip count is zero -- a texture the RHI refuses to create. */
	TArray<FString> FindZeroSizeDynamicTextures();

	/** DreamGUI canvas meshes in InWorld that the level would save, copy or duplicate for play: every one without RF_Transient. */
	TArray<FString> FindPersistentCanvasMeshes(const UWorld* InWorld);

	/** Objects of InClass (or a subclass) whose outermost package is InPackage. */
	TArray<FString> FindObjectsInPackage(const UPackage* InPackage, const UClass* InClass);

	/** FrameCount DreamGUI frames in InWorld: the manager's tick, then the draw-call submission that ends a frame. */
	void DrawFrames(UWorld* InWorld, int32 FrameCount);

	/**
	 * InActor copied the way the level editor's Copy does it, into text rather than the clipboard, and
	 * pasted into InWorld's current level Times times, the way Paste does. Returns the pasted actors.
	 * The editor's actor selection is left empty.
	 */
	TArray<AActor*> CopyPasteActor(UWorld* InWorld, AActor* InActor, int32 Times, FString* OutCopiedText = nullptr);

	/**
	 * The world duplication a play-in-editor session starts with (UWorld::GetDuplicatedWorldForPIE,
	 * with the package the editor makes for it). The copy is not initialised: what matters is what the
	 * duplication and the PostLoad of every duplicated object did. DestroyDuplicatedWorld removes it.
	 */
	UWorld* DuplicateWorldForPlayInEditor(UWorld* InWorld);
	void DestroyDuplicatedWorld(UWorld* InDuplicate);
}
