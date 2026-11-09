// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "DreamNiagaraTrigger.generated.h"

class UDreamNiagaraSystem;
class UDreamWidget;

/** What a trigger does to its Niagara system. */
UENUM(BlueprintType)
enum class EDreamNiagaraTriggerAction : uint8
{
	None,
	/** BurstAt where the pointer is. */
	BurstAtPointer,
	/** BurstAt the system widget's centre. */
	BurstAtCentre,
	/** BurstTo the flight target, from the pointer. */
	BurstToTarget,
	Activate,
	Deactivate,
	/** Clears the particles and starts again. */
	Restart,
};

/**
 * Plays a widget's Niagara system on the widget's pointer events -- a click, the pointer coming in and going out -- and when a
 * progress the trigger watches fills: the wiring every UI effect needs, without a Blueprint. Put it on the widget that takes the
 * pointer; the system is the visual of SystemWidget, else of this widget or the first below it that has one.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent, DisplayName = "Niagara Trigger"))
class DREAMGUINIAGARA_API UDreamNiagaraTrigger : public UDreamUIBehaviour, public IDreamPointerClickInterface, public IDreamPointerEnterExitInterface
{
	GENERATED_BODY()

public:
	UDreamNiagaraTrigger();

	/** Does InAction now; a pointer action takes the last pointer position the trigger saw, else the centre. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Niagara")
	void Fire(EDreamNiagaraTriggerAction InAction);

	/** The Niagara system the trigger plays. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Niagara")
	UDreamNiagaraSystem* FindSystem()const;

	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;

protected:
	virtual void Tick(float DeltaTime)override;

	/** The widget whose visual is the system; unset, this widget or the first below it with one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara")
	TObjectPtr<UDreamWidget> SystemWidget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara")
	EDreamNiagaraTriggerAction OnClick = EDreamNiagaraTriggerAction::BurstAtPointer;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara")
	EDreamNiagaraTriggerAction OnEnter = EDreamNiagaraTriggerAction::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara")
	EDreamNiagaraTriggerAction OnExit = EDreamNiagaraTriggerAction::None;
	/** Where BurstToTarget sends its particles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara")
	TObjectPtr<UDreamWidget> FlightTarget;
	/** Found by its display name in the same tree, when FlightTarget is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara")
	FName FlightTargetName;

	/** A widget with a float Percent (a progress bar): when it reaches 1, OnProgressFull is done. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara|Progress")
	TObjectPtr<UDreamWidget> ProgressWidget;
	/** Found by its display name in the same tree, when ProgressWidget is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara|Progress")
	FName ProgressWidgetName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara|Progress")
	EDreamNiagaraTriggerAction OnProgressFull = EDreamNiagaraTriggerAction::BurstAtCentre;
	/** What a click adds to the progress (a charge bar): 0 adds nothing. A full bar starts again from 0 at the next click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Niagara|Progress", meta = (ClampMin = "0", ClampMax = "1"))
	float ClickAddsProgress = 0.0f;

private:
	UDreamWidget* FindNamed(FName InName)const;
	UDreamWidget* ResolveFlightTarget()const;
	UDreamWidget* ResolveProgressWidget()const;
	/** Where InWorldPoint is in the system widget's local space. */
	FVector2D ToSystemLocal(const FVector& InWorldPoint)const;
	bool ReadProgress(float& OutPercent)const;
	void WriteProgress(float InPercent)const;

	FVector2D LastPointerLocal = FVector2D::ZeroVector;
	bool bHasPointer = false;
	bool bProgressWasFull = false;
};
