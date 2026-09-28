// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DreamWidgetTreeHost.generated.h"

/** Why a host lets go of its tree. */
enum class EDreamTreeReleaseReason : uint8
{
	/** The tree's class is being recompiled. The host rebuilds from the new class afterwards. */
	Recompile,
	/** An undo or a redo changed what the host shows. The host rebuilds afterwards. */
	Undo,
	/** The host's level is leaving its world: streamed out, or hidden in the editor. */
	LevelRemoved,
	/** The host itself is going away. Nothing is rebuilt. */
	HostDestroyed,
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamWidgetTreeHost : public UInterface
{
	GENERATED_BODY()
};

/**
 * Something that owns widget trees: a world widget component, the screen and popup layers, the input
 * services that hold widgets of their own, a designer preview.
 *
 * Ownership in the UObject sense. The host is the Outer of every tree it owns -- so a widget still finds
 * its world through it (UDreamWidget::GetWorld) -- and holds each tree by a property declared Transient,
 * DuplicateTransient and TextExportTransient, so nothing that saves, duplicates or copies the host takes
 * a tree along. The manager only observes. A registered tree whose host is gone is a bug to report, not
 * something for the garbage collector to clean up.
 */
class DREAMGUI_API IDreamWidgetTreeHost
{
	GENERATED_BODY()

public:
	/** The Outer for this host's trees. Its Outer chain must reach the UWorld the host is in. */
	virtual UObject* GetTreeOuter() const = 0;

	/** Tear the tree down now, for InReason. Idempotent: a host with no tree does nothing. */
	virtual void ReleaseTree(EDreamTreeReleaseReason InReason) = 0;

	/** Build the tree again from its class, after a release that was not a destruction. */
	virtual void RebuildTree() = 0;
};
