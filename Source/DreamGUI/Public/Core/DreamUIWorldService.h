// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Algo/StableSort.h"

class UWorld;

/**
 * A world-level DreamGUI service as its world's teardown sees it: the manager, the screen and popup layers,
 * and the input services -- tooltips, modals, drag and drop, the virtual cursor, action routing, navigation.
 *
 * The engine deinitializes world subsystems in the order it created them. InitializeDependency makes that
 * the right order to set up in and the wrong order to tear down in, and the services live in more than one
 * module, so no single class can own them all without the dependencies pointing the wrong way. Teardown is
 * therefore not left to Deinitialize. Each service registers with its world's manager when it initializes,
 * from whichever module it lives in, and the manager takes them all down on the one path it owns -- highest
 * priority first -- before it clears its own state. Deinitialize is then only a check that teardown has
 * already happened.
 *
 * Nothing implements this yet: the services still tear down in Deinitialize.
 */
class IDreamUIWorldService
{
public:
	virtual ~IDreamUIWorldService() = default;

	/** Higher tears down first. The bands are in DreamUI::WorldServiceTeardownPriority. */
	virtual int32 GetTeardownPriority() const = 0;

	/** Let go of everything this service holds in InWorld -- captures, drags, roots. Called once, outside garbage collection. */
	virtual void TeardownForWorld(UWorld& InWorld) = 0;
};

namespace DreamUI
{
	/**
	 * The bands a world's teardown goes through, highest first: the input services, which stop captures,
	 * drags and cursors; then the layers that hold roots; then the hosts' trees.
	 */
	namespace WorldServiceTeardownPriority
	{
		inline constexpr int32 Input = 300;
		inline constexpr int32 Layers = 200;
		inline constexpr int32 Hosts = 100;
	}

	/** InOutServices in the order teardown visits them: priority high to low, and registration order among equals. */
	inline void SortForTeardown(TArray<IDreamUIWorldService*>& InOutServices)
	{
		Algo::StableSort(InOutServices, [](const IDreamUIWorldService* A, const IDreamUIWorldService* B)
		{
			return A->GetTeardownPriority() > B->GetTeardownPriority();
		});
	}
}
