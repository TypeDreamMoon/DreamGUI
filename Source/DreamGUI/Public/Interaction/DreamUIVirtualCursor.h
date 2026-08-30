// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamUIVirtualCursor.generated.h"

class UDreamWidget;
class UDreamUserWidget;
class UDreamStandaloneInputModule;
enum class EDreamUIInputDevice : uint8;

/**
 * A pointer for a player who has no pointer. Four-way navigation cannot drive a map, a skill tree
 * or a crafting grid; this integrates the left stick into a screen position, substitutes it for the
 * OS mouse through the input module's override seam, and turns the confirm button into the left
 * mouse button. Everything downstream -- raycast, hover, click, drag, tooltips -- cannot tell the
 * difference, which is the entire design.
 *
 * Activate it yourself from screens that need it, or set bAutoVirtualCursorOnGamepad in the
 * settings to follow the physical device: gamepad in hand shows the cursor, any other device hides
 * it again.
 */
UCLASS()
class DREAMGUI_API UDreamUIVirtualCursorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI Virtual Cursor Subsystem"), Category = "DreamGUI|VirtualCursor")
	static UDreamUIVirtualCursorSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void ActivateVirtualCursor();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|VirtualCursor")
	void DeactivateVirtualCursor();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|VirtualCursor")
	bool IsVirtualCursorActive() const { return bActive; }

private:
	UDreamStandaloneInputModule* GetInputModule() const;
	void EnsureAutoModeSubscribed();
	void HandleInputDeviceChanged(EDreamUIInputDevice InDevice);
	void UpdateCursorVisualPosition();
	void DestroyCursorVisual();

	bool bActive = false;
	bool bAutoModeSubscribed = false;
	/** Viewport-space cursor position, top-left origin, integrated from the stick. */
	FVector2D CursorPosition = FVector2D::ZeroVector;
	/** Confirm-button state we last pushed, so press/release edges are delivered exactly once. */
	bool bConfirmDown = false;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> CursorHolder;
	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> CursorWidget;
};
