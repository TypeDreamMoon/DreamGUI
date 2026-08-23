// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "DreamEnhancedInputEventSystemActor.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * The event system preset for projects on Enhanced Input.
 *
 * This is the C++ form of the DreamEventSystemActor_EnhancedInput preset Blueprint. Only the mouse
 * half differs from the legacy preset: the three buttons and the wheel arrive as Input Actions from
 * a mapping context this actor pushes, while navigation keys and touch stay on the legacy bindings
 * it inherits. Enhanced Input has no equivalent of the Mouse2D vector axis that the legacy preset
 * listens to, so mouse movement is polled on tick instead -- which is what the Blueprint did too.
 *
 * The four actions and the context default to the ones shipped in the plugin, and every one is
 * EditDefaultsOnly so a project can point them at its own.
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamEnhancedInputEventSystemActor : public ADreamStandaloneInputEventSystemActor
{
	GENERATED_BODY()

public:
	ADreamEnhancedInputEventSystemActor();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** Replaces the legacy mouse bindings with the four Input Actions. Navigation and touch are inherited. */
	virtual void BindMouseInput() override;

	/** Pushed onto the local player on BeginPlay so the actions below resolve without project setup. */
	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	/** Priority for the pushed context. Above the default 0 so UI input is not eaten by gameplay. */
	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	int32 MappingContextPriority = 0;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> TriggerLeftAction;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> TriggerRightAction;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> TriggerMiddleAction;

	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI|Enhanced Input")
	TObjectPtr<UInputAction> MouseWheelAction;

private:
	void AddMappingContextToLocalPlayer();

	void OnTriggerLeft(const FInputActionValue& Value);
	void OnTriggerRight(const FInputActionValue& Value);
	void OnTriggerMiddle(const FInputActionValue& Value);
	void OnMouseWheelAction(const FInputActionValue& Value);

	/** Shared by the three button actions; each one only differs by which button type it reports. */
	void ForwardTrigger(const FInputActionValue& Value, EDreamUIMouseButtonType ButtonType);
};
