// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSequenceTickInterval.h"
#include "MovieSceneSequenceTickManagerClient.h"
#include "UObject/Object.h"
#include "DreamUIAnimationTicker.generated.h"

class UDreamWidgetAnimationPlayer;
class UMovieSceneSequenceTickManager;

/**
 * What ticks the animation players DreamGUI's components make, instead of the world's sequence tick manager.
 *
 * The world's tick manager ticks every client registered with it, every frame: a weak look-up, a shared pointer copied and a
 * virtual call each, playing or not -- and a wall of widgets keeps thousands of players, most of them waiting for their next
 * play (UDreamWidgetAnimationComponent::SparePlayers). The weak look-ups alone were a twentieth of the screen-space wall's
 * frame. A ticker ticks only the players with something to do (Activate), from a list it holds them in.
 *
 * - One ticker per world and tick interval, registered with the world's tick manager as one client of its own: it is ticked
 *   where and when its players would have been, with the delta they would have had -- the same interval, the same pause rule.
 * - Its players register with a tick manager of the ticker's (GetRegistry) that nothing ticks: Initialize wants one to
 *   register with, and their linker, runner and latent actions are that tick manager's. The ticker flushes that runner and
 *   runs those latent actions after its players, as the world's tick manager does after its own.
 * - A player is let go once it has nothing to do -- not playing, its clock ready, nothing to sync -- and is ticked again
 *   from the next play (UDreamWidgetAnimationPlayer::KeepTicked).
 * - DreamUI.Animation.Ticker 0: players made from then on register with the world's tick manager, as before.
 */
UCLASS(Transient)
class DREAMGUI_API UDreamUIAnimationTicker : public UObject, public IMovieSceneSequenceTickManagerClient
{
	GENERATED_BODY()

public:
	/** Whether players made now are ticked by a ticker (DreamUI.Animation.Ticker). */
	static bool IsEnabled();
	/** InWorld's ticker for InInterval, made and registered the first time it is asked for. Null without a world. */
	static UDreamUIAnimationTicker* FindOrCreate(UWorld* InWorld, const FMovieSceneSequenceTickInterval& InInterval);

	/** The tick manager the ticker's players register with: nothing ticks it but this ticker. */
	UMovieSceneSequenceTickManager* GetRegistry() const { return Registry; }

	/** InPlayer is ticked from this ticker's next tick on, until it has nothing to do. */
	void Activate(UDreamWidgetAnimationPlayer& InPlayer);
	/** InPlayer is going: out of the list, if it is in it. */
	void Forget(UDreamWidgetAnimationPlayer& InPlayer);
	/** How many players the ticker would tick now: what tests count. */
	int32 GetActiveCount() const { return ActivePlayers.Num(); }

	//~ IMovieSceneSequenceTickManagerClient
	virtual void TickFromSequenceTickManager(float DeltaSeconds, FMovieSceneEntitySystemRunner* InRunner) override;

	//~ UObject
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void BeginDestroy() override;

private:
	void RemoveActive(UDreamWidgetAnimationPlayer& InPlayer);

	/** See GetRegistry. */
	UPROPERTY()
	TObjectPtr<UMovieSceneSequenceTickManager> Registry;
	/** The world's tick manager this ticker is a client of, told when the ticker goes. */
	TWeakObjectPtr<UMovieSceneSequenceTickManager> WorldTickManager;
	FMovieSceneSequenceTickInterval Interval;
	/**
	 * The players with something to do. Each keeps its index here (UDreamWidgetAnimationPlayer::TickerActiveIndex). Held for
	 * the collector (AddReferencedObjects), which clears an entry whose player is garbage; a cleared entry is let go at the
	 * next tick.
	 */
	TArray<TObjectPtr<UDreamWidgetAnimationPlayer>> ActivePlayers;
	/** Counted up each tick: a player that is ticked is not ticked again in the same tick, whatever joins the list meanwhile. */
	uint64 TickSerial = 0;
	/** Set while the players are ticked: one that joins meanwhile is ticked from the next tick (Activate). */
	bool bTicking = false;
};
