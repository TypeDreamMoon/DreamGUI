// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

class UWorld;

namespace DreamUI
{
	/**
	 * Flags for every object DreamGUI makes while running and keeps in a property: render targets,
	 * dynamic material instances, data textures, the canvas mesh, body setups.
	 *
	 * - RF_Transient: the level or asset that holds the owner does not save it.
	 * - RF_DuplicateTransient: a copy of the owner -- above all the world a play session duplicates --
	 *   gets null in its place instead of a clone. The clone is the dangerous part: a UTexture2DDynamic
	 *   comes out 0x0, and cloning an object clones its whole outer chain with it, which is how one
	 *   reference drags a widget tree into the copy.
	 * - RF_TextExportTransient: copy and paste leaves it out of the copied text, which would otherwise
	 *   paste it back as an ordinary, saved sub-object of the new actor.
	 *
	 * RF_Transient alone stops only the first of the three.
	 */
	inline constexpr EObjectFlags RuntimeObjectFlags = RF_Transient | RF_DuplicateTransient | RF_TextExportTransient;

	/**
	 * For PostDuplicate of the classes a play session's copy of a world must never hold: widgets, their
	 * behaviours, canvas meshes, data textures. Reaching one means something the level keeps still refers
	 * into a widget tree, directly or through an outer. The copy is logged with its path and counted, and
	 * the first one of each play session ensures, so the reference shows up where it is crossed rather
	 * than as a crash further on. FindTreeBridges (DreamGUI.Diag.FindTreeBridges) lists such references.
	 */
	DREAMGUI_API void ReportCopiedIntoPlaySession(const UObject& InCopy);

	/** How many copies ReportCopiedIntoPlaySession has been told about since the process started. */
	DREAMGUI_API int32 GetCopiedIntoPlaySessionCount();

	/**
	 * While one is alive, a copy is still logged and counted but does not ensure: for a test that makes
	 * one on purpose.
	 */
	struct DREAMGUI_API FScopedExpectedCopiesIntoPlaySession
	{
		FScopedExpectedCopiesIntoPlaySession();
		~FScopedExpectedCopiesIntoPlaySession();
		UE_NONCOPYABLE(FScopedExpectedCopiesIntoPlaySession);
	};

	/**
	 * Whether the transaction buffer keeps InObject: it is transactional and not transient. The engine
	 * records a transient object like any other once it is transactional -- SaveToTransactionBuffer does
	 * not look at RF_Transient -- so an undo would bring back a tree its host had destroyed.
	 */
	DREAMGUI_API bool IsKeptByUndo(const UObject& InObject);

	/**
	 * Modify() when IsKeptByUndo, and nothing otherwise. Modify() on an object the buffer does not keep
	 * marks its package dirty instead, which for a widget in the level editor's world is the map.
	 */
	DREAMGUI_API void ModifyIfKeptByUndo(UObject& InObject);

	/**
	 * RF_Transactional for a widget a DreamGUI API makes inside InOuter when undo keeps InOuter -- an
	 * authored tree, in an asset or in a level -- and no flags otherwise: not in a tree made while a world
	 * runs, which is transient, and not straight in a world, which is where the runtime API puts the
	 * widgets it makes. Those live as long as whoever built them; undo has no business restoring them.
	 */
	DREAMGUI_API EObjectFlags TransactionalFlagFor(const UObject* InOuter);

	/** A reference from an object a level keeps into a widget tree. */
	struct FTreeBridge
	{
		/** The object the level keeps: an actor, or one of its components or other sub-objects. */
		FString From;
		/** The object in the tree it refers to. */
		FString To;
		/** The actor whose presenter loaded that tree; empty when no presenter in the world did. */
		FString Host;
		/** Whether From belongs to that same actor. */
		bool bIntoOwnHost = false;
	};

	/**
	 * Every persistent reference -- one a save or a play session's duplication follows -- from an object
	 * the levels of InWorld keep into a widget tree of InWorld. Each is a way for a copy of the level to
	 * clone part of a tree. One into another actor's tree is the kind a paste leaves behind.
	 */
	DREAMGUI_API TArray<FTreeBridge> FindTreeBridges(const UWorld& InWorld);

	/**
	 * Run InFunction on the engine's core ticker after InTicks more ticks -- 0 is the next one. The deferral
	 * an editor edit needs -- until the property change, the drag or the compile under way has finished --
	 * held by the ticker rather than by anything of DreamGUI's own. Capture weakly: what the function works on
	 * may be gone by the time it runs.
	 */
	DREAMGUI_API void DeferToLaterTick(TFunction<void()> InFunction, int32 InTicks = 0);
}
