// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIInputServices.h"
#include "DreamUIInputSubsystem.generated.h"

class AActor;
class UDreamEventSystem;
class UDreamUIManagerWorldSubsystem;

/**
 * The input system's side of UDreamUIInputServices, one per world: what the core asks for, answered from
 * the event systems, the action router and drag and drop.
 *
 * It also owns the world's event-system registry -- one event system per local player index -- and the
 * interaction objects it had to create for a player: the transient host actor carrying that player's
 * default raycasters, and the event system spawned from UDreamGUISettings::EventSystemActorClass.
 *
 * Created in the same worlds as UDreamUIManagerWorldSubsystem, because every question the core asks here
 * is about a widget that manager registered.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMGUIINPUT_API UDreamUIInputSubsystem : public UDreamUIInputServices
{
	GENERATED_BODY()

public:
	/** InWorldContext's world's input subsystem, or null. */
	static UDreamUIInputSubsystem* Get(const UObject* InWorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void PostInitialize() override;
	virtual void Deinitialize() override;

	// ---- the event-system registry

	/** The event system registered for local player InUserIndex, or null. */
	UDreamEventSystem* GetEventSystemByUserIndex(int32 InUserIndex = 0) const;
	const TMap<int32, TWeakObjectPtr<UDreamEventSystem>>& GetMapUserIndexToEventSystem() const { return MapUserIndexToEventSystem; }
	/**
	 * Register InEventSystem under its UserIndex. A second live event system for an index that already has
	 * one is refused, with an error: two would make each player read the other's input.
	 */
	void AddEventSystem(UDreamEventSystem* InEventSystem);
	/** Remove InEventSystem -- by identity, so a late unregister cannot evict the index's new owner. */
	void RemoveEventSystem(UDreamEventSystem* InEventSystem);

	/** The host actor carrying InUserIndex's auto-created raycasters, or null if none was needed. */
	AActor* GetInteractionHost(int32 InUserIndex) const;

	//~ Begin UDreamUIInputServices
	virtual bool SetFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) override;
	virtual bool HasFocus(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) const override;
	virtual void ClearFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) override;
	virtual bool HasFocusedDescendant(const UDreamWidget* InWidget, int32 InUserIndex) const override;
	virtual bool IsHovered(const UDreamWidget* InWidget, int32 InUserIndex) const override;
	virtual bool HasMouseCapture(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerIndex) const override;
	virtual UDreamPointerEventData* FindPointer(int32 InUserIndex, int32 InPointerId) const override;
	virtual bool CanListenForActions() const override;
	virtual FDreamUIActionHandle RegisterWidgetAction(UDreamWidget* InOwner, const FDataTableRowHandle& InAction,
		FDreamUIActionExecutedDelegate InCallback, int32 InUserIndex, bool bInDisplayInActionBar) override;
	virtual void UnregisterAction(const FDreamUIActionHandle& InHandle) override;
	virtual bool IsDragDropping() const override;
	virtual UDreamDragDropOperation* GetDragOperationForPointer(int32 InPointerId) const override;
	virtual bool CancelActiveDrag() override;
	/**
	 * The event system half is per world, not per player -- only the first local player gets one spawned
	 * from UDreamGUISettings::EventSystemActorClass, because a second copy would carry the same UserIndex and
	 * make each player read the other's input; a second player's event system has to be placed
	 * deliberately, and not having one is a warning rather than a guess.
	 *
	 * The raycaster half is skipped when that player already has one of that kind, wherever it was placed,
	 * which is what lets an authored raycaster override the default. Otherwise one is added to a transient
	 * "DreamInteractionHost_P%d" actor.
	 */
	virtual void EnsureInteractionForPlayer(int32 InUserIndex, EDreamInteractionKind InKind) override;
	virtual void PrepareScreenInteraction(UDreamCanvas* InRootCanvas, int32 InUserIndex) override;
	//~ End UDreamUIInputServices

private:
#if WITH_EDITOR
	/** The selectables' navigation arrows, drawn when the manager draws its editor helpers. */
	void DrawNavigationVisualizers(UDreamUIManagerWorldSubsystem* InManager);
#endif

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TMap<int32, TWeakObjectPtr<UDreamEventSystem>> MapUserIndexToEventSystem;

	/**
	 * One transient actor per local player, carrying whichever raycasters were created for them.
	 *
	 * Per PLAYER rather than per kind, so a player pointing at both a screen UI and a world-space panel has
	 * one host with two raycasters on it instead of two actors that mean the same thing.
	 */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<AActor>> InteractionHosts;

	/** The event system spawned from project settings, if one had to be. Never more than one. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> CreatedEventSystemActor;
};
