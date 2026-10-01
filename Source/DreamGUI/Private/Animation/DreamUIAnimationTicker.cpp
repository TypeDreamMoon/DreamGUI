// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Animation/DreamUIAnimationTicker.h"

#include "Animation/DreamWidgetAnimationPlayer.h"
#include "Engine/World.h"
#include "UObject/ObjectKey.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "HAL/IConsoleManager.h"
#include "MovieSceneSequenceTickManager.h"

static TAutoConsoleVariable<int32> CVarDreamUIAnimationTicker(
	TEXT("DreamUI.Animation.Ticker"),
	1,
	TEXT("1: the animation players DreamGUI's components make are ticked by DreamGUI's animation ticker, which ticks only the ones ")
	TEXT("with something to do. 0: they register with the world's sequence tick manager, which ticks every one every frame. ")
	TEXT("Applies to players made from then on."),
	ECVF_Default);

namespace DreamUIAnimationTickerLocal
{
	/** A world's tickers, one for each rounded tick interval and pause rule: found by these, not by name. */
	struct FWorldTickers
	{
		TArray<TWeakObjectPtr<UDreamUIAnimationTicker>> Tickers;
		TArray<TPair<int32, bool>> Keys;
	};
	/** Game thread only, as the players are. Kept alive by their players (UDreamWidgetAnimationPlayer::Ticker), not by this. */
	TMap<TObjectKey<UWorld>, FWorldTickers> TickersByWorld;
}

bool UDreamUIAnimationTicker::IsEnabled()
{
	return CVarDreamUIAnimationTicker.GetValueOnGameThread() != 0;
}

UDreamUIAnimationTicker* UDreamUIAnimationTicker::FindOrCreate(UWorld* InWorld, const FMovieSceneSequenceTickInterval& InInterval)
{
	if (InWorld == nullptr)
	{
		return nullptr;
	}
	using namespace DreamUIAnimationTickerLocal;
	const TPair<int32, bool> Key(InInterval.RoundTickIntervalMs(), InInterval.bTickWhenPaused);
	FWorldTickers& WorldTickers = TickersByWorld.FindOrAdd(InWorld);
	for (int32 Index = WorldTickers.Keys.Num() - 1; Index >= 0; --Index)
	{
		UDreamUIAnimationTicker* Existing = WorldTickers.Tickers[Index].Get();
		if (Existing == nullptr || Existing->Registry == nullptr)
		{
			// Collected once its last player was: made again when asked for.
			WorldTickers.Tickers.RemoveAtSwap(Index);
			WorldTickers.Keys.RemoveAtSwap(Index);
			continue;
		}
		if (WorldTickers.Keys[Index] == Key)
		{
			return Existing;
		}
	}
	UMovieSceneSequenceTickManager* WorldTickManager = UMovieSceneSequenceTickManager::Get(InWorld);
	if (WorldTickManager == nullptr)
	{
		return nullptr;
	}
	UDreamUIAnimationTicker* Ticker = NewObject<UDreamUIAnimationTicker>(InWorld,
		MakeUniqueObjectName(InWorld, StaticClass(), TEXT("DreamUIAnimationTicker")));
	Ticker->Interval = InInterval;
	// Outered to the world like the world's own tick manager, and named apart from it: nothing ticks it, and nothing but the
	// ticker knows it, which hands it to its players to register with.
	Ticker->Registry = NewObject<UMovieSceneSequenceTickManager>(InWorld,
		MakeUniqueObjectName(InWorld, UMovieSceneSequenceTickManager::StaticClass(), TEXT("DreamUIAnimationTickerRegistry")));
	Ticker->WorldTickManager = WorldTickManager;
	WorldTickManager->RegisterTickClient(InInterval, Ticker);
	WorldTickers.Tickers.Add(Ticker);
	WorldTickers.Keys.Add(Key);
	return Ticker;
}

void UDreamUIAnimationTicker::Activate(UDreamWidgetAnimationPlayer& InPlayer)
{
	if (InPlayer.TickerActiveIndex != INDEX_NONE)
	{
		return;
	}
	InPlayer.TickerActiveIndex = ActivePlayers.Add(&InPlayer);
	// Joined during a tick: ticked from the next one, as a client the world's tick manager is given during its tick is.
	if (bTicking)
	{
		InPlayer.LastTickerTick = TickSerial;
	}
}

void UDreamUIAnimationTicker::RemoveActive(UDreamWidgetAnimationPlayer& InPlayer)
{
	const int32 Index = InPlayer.TickerActiveIndex;
	InPlayer.TickerActiveIndex = INDEX_NONE;
	if (!ActivePlayers.IsValidIndex(Index) || ActivePlayers[Index] != &InPlayer)
	{
		return;
	}
	ActivePlayers.RemoveAtSwap(Index, EAllowShrinking::No);
	if (ActivePlayers.IsValidIndex(Index))
	{
		if (UDreamWidgetAnimationPlayer* Moved = ActivePlayers[Index])
		{
			Moved->TickerActiveIndex = Index;
		}
	}
}

void UDreamUIAnimationTicker::Forget(UDreamWidgetAnimationPlayer& InPlayer)
{
	RemoveActive(InPlayer);
}

void UDreamUIAnimationTicker::TickFromSequenceTickManager(float DeltaSeconds, FMovieSceneEntitySystemRunner* InRunner)
{
	UMovieSceneSequenceTickManager* const RegistryManager = Registry.Get();
	if (RegistryManager == nullptr)
	{
		return;
	}
	// The runner the players queue their evaluations in: the registry's, for this interval. InRunner is the world's, for its
	// own clients, and flushed by it.
	const TSharedPtr<FMovieSceneEntitySystemRunner> Runner = RegistryManager->GetRunner(Interval);
	if (Runner.IsValid() && Runner->IsCurrentlyEvaluating())
	{
		// A budgeted evaluation still going: what the world's tick manager does for a group in that state.
		return;
	}
	++TickSerial;
	TGuardValue<bool> Ticking(bTicking, true);
	// By index over the list as it grows -- a tick may start another player's play, which joins at the end, marked ticked
	// (Activate) -- and a player let go takes the last one's place.
	for (int32 Index = 0; Index < ActivePlayers.Num();)
	{
		UDreamWidgetAnimationPlayer* const Player = ActivePlayers[Index];
		if (Player == nullptr)
		{
			// Cleared by the collector: garbage, and nothing to tick.
			ActivePlayers.RemoveAtSwap(Index, EAllowShrinking::No);
			if (ActivePlayers.IsValidIndex(Index) && ActivePlayers[Index] != nullptr)
			{
				ActivePlayers[Index]->TickerActiveIndex = Index;
			}
			continue;
		}
		if (Player->LastTickerTick != TickSerial)
		{
			Player->LastTickerTick = TickSerial;
			Player->TickFromSequenceTickManager(DeltaSeconds, Runner.Get());
		}
		// Where it is now: a listener of its tick may have let other players go, or this one.
		if (!ActivePlayers.IsValidIndex(Index) || ActivePlayers[Index] != Player)
		{
			continue;
		}
		if (Player->HasNothingToTick())
		{
			RemoveActive(*Player);
			continue;
		}
		++Index;
	}
	bTicking = false;
	// What the world's tick manager does after its clients: the evaluations they queued, then the latent actions.
	if (Runner.IsValid() && Runner->HasQueuedUpdates())
	{
		Runner->Flush();
	}
	RegistryManager->RunLatentActions();
}

void UDreamUIAnimationTicker::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UDreamUIAnimationTicker* This = CastChecked<UDreamUIAnimationTicker>(InThis);
	Collector.AddReferencedObjects(This->ActivePlayers);
}

void UDreamUIAnimationTicker::BeginDestroy()
{
	if (UMovieSceneSequenceTickManager* Manager = WorldTickManager.Get())
	{
		Manager->UnregisterTickClient(this);
	}
	WorldTickManager.Reset();
	Super::BeginDestroy();
}
