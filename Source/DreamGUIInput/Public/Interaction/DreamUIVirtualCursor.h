// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/DreamUIWorldService.h"
#include "DreamUIVirtualCursor.generated.h"

class UDreamWidget;
class UDreamUserWidget;
class UDreamStandaloneInputModule;
class UDreamEventSystem;
class UDreamUIInputSubsystem;
enum class EDreamUIInputDevice : uint8;

/** One player's virtual cursor. */
USTRUCT()
struct FDreamUIVirtualCursorUserState
{
	GENERATED_BODY()

	bool bActive = false;
	/** Viewport-space cursor position, top-left origin, integrated from the player's stick. */
	FVector2D CursorPosition = FVector2D::ZeroVector;
	/** Confirm-button state last pushed, so press/release edges are delivered exactly once. */
	bool bConfirmDown = false;
	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> CursorHolder;
	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> CursorWidget;
};

/**
 * A pointer for a player who has no pointer. Four-way navigation cannot drive a map, a skill tree or a crafting
 * grid; this integrates the player's left stick into a screen position, substitutes it for the OS mouse through
 * their input module's override seam, and turns the confirm button into the left mouse button. Everything
 * downstream -- raycast, hover, click, drag, tooltips -- cannot tell the difference, which is the entire design.
 *
 * One cursor per player, each driven by that player's own stick and drawn on that player's screen.
 *
 * Activate it yourself from screens that need it, or set bAutoVirtualCursorOnGamepad in the settings to follow
 * the physical device: gamepad in hand shows the cursor, any other device hides it again.
 */
UCLASS()
class DREAMGUIINPUT_API UDreamUIVirtualCursorSubsystem : public UTickableWorldSubsystem, public IDreamUIWorldService
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI Virtual Cursor Subsystem"), Category = "DreamGUI|VirtualCursor")
	static UDreamUIVirtualCursorSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual int32 GetTeardownPriority() const override { return DreamUI::WorldServiceTeardownPriority::Input; }
	virtual void TeardownForWorld(UWorld& InWorld) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/** Ticks while the game is paused: a gamepad player in a pause menu has no other pointer at all. */
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** The first player's cursor. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void ActivateVirtualCursor();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void DeactivateVirtualCursor();
	UFUNCTION(BlueprintPure, Category = "DreamGUI|VirtualCursor")
	bool IsVirtualCursorActive() const;
	/**
	 * Push the first player's confirm button, as a left mouse button at their cursor. Edge-triggered: only a change
	 * from what was last pushed is delivered. The preset actor calls it after offering the key to the action
	 * router, so a confirm the router claimed never becomes a click.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void SetConfirmPressed(bool bInPressed);
	/** Where the first player's cursor is, in viewport coordinates. Meaningless while it is inactive. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|VirtualCursor")
	FVector2D GetVirtualCursorPosition() const;

	/** Player InUserIndex's cursor. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void ActivateVirtualCursorForUser(int32 InUserIndex);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void DeactivateVirtualCursorForUser(int32 InUserIndex);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|VirtualCursor")
	bool IsVirtualCursorActiveForUser(int32 InUserIndex) const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void SetConfirmPressedForUser(int32 InUserIndex, bool bInPressed);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|VirtualCursor")
	FVector2D GetVirtualCursorPositionForUser(int32 InUserIndex) const;

private:
	UDreamStandaloneInputModule* GetInputModule(int32 InUserIndex) const;
	void HandleInputDeviceChanged(int32 InUserIndex, EDreamUIInputDevice InDevice);
	void UpdateCursorVisualPosition(int32 InUserIndex, FDreamUIVirtualCursorUserState& InState);
	void DestroyCursorVisual(FDreamUIVirtualCursorUserState& InState);

	/** The input subsystem listened to, so the teardown can stop listening. */
	TWeakObjectPtr<UDreamUIInputSubsystem> InputSubsystem;
	/** Set by TeardownForWorld, which runs once. */
	bool bTornDownForWorld = false;

	/** Each player's cursor, by player index. */
	UPROPERTY(Transient)
	TMap<int32, FDreamUIVirtualCursorUserState> UserStates;
};
