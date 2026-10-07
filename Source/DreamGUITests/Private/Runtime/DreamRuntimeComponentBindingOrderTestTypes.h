// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamRuntimeComponentBindingOrderTestTypes.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamRuntimeComponentBindingOrderEvent);

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class UDreamRuntimeComponentBindingOrderTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	FName Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float BoundValue = 0.0f;

	UPROPERTY(BlueprintAssignable, Category = "Test")
	FDreamRuntimeComponentBindingOrderEvent OnTriggered;

	UFUNCTION(BlueprintCallable, Category = "Test")
	void SetBoundValue(float InValue) { BoundValue = InValue; }

	UFUNCTION(BlueprintCallable, Category = "Test")
	void Trigger() { OnTriggered.Broadcast(); }
};

UCLASS(Blueprintable)
class UDreamRuntimeComponentBindingOrderTestWidget : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	bool bReorderOnInitialized = true;

	int32 TriggerCount = 0;

	UFUNCTION(BlueprintPure, Category = "Test")
	float ReadBoundValue() const { return 77.0f; }

	UFUNCTION(BlueprintCallable, Category = "Test")
	void HandleTrigger() { ++TriggerCount; }

	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();
		if (bReorderOnInitialized && GetWidgetTree() != nullptr)
		{
			if (UDreamWidget* Subject = GetWidgetTree()->FindWidgetByVariableName(TEXT("Subject")))
			{
				if (Subject->GetAllComponents().Num() == 2)
				{
					Subject->MoveComponentToIndex(Subject->GetAllComponents()[1], 0);
				}
			}
		}
	}
};
