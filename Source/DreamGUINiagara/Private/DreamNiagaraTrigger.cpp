// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraTrigger.h"

#include "Core/Components/DreamWidget.h"
#include "DreamNiagaraSystem.h"
#include "Event/DreamPointerEventData.h"
#include "UObject/UnrealType.h"

namespace DreamNiagaraTriggerLocal
{
	const FName PercentName(TEXT("Percent"));

	UDreamNiagaraSystem* SystemOf(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? Cast<UDreamNiagaraSystem>(InWidget->GetVisual()) : nullptr;
	}

	/** InWidget's system, else the first of its descendants', depth first. */
	UDreamNiagaraSystem* FindBelow(const UDreamWidget* InWidget)
	{
		if (UDreamNiagaraSystem* Found = SystemOf(InWidget))
		{
			return Found;
		}
		if (InWidget != nullptr)
		{
			for (const UDreamWidget* Child : InWidget->GetChildren())
			{
				if (UDreamNiagaraSystem* Found = FindBelow(Child))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}
}

UDreamNiagaraTrigger::UDreamNiagaraTrigger()
{
	// It ticks to watch a progress, and only when it has one to watch.
	bTickEvenWhenPaused = true;
}

UDreamNiagaraSystem* UDreamNiagaraTrigger::FindSystem()const
{
	return DreamNiagaraTriggerLocal::FindBelow(SystemWidget != nullptr ? SystemWidget.Get() : GetWidget());
}

UDreamWidget* UDreamNiagaraTrigger::FindNamed(FName InName)const
{
	const UDreamWidget* Widget = GetWidget();
	if (InName.IsNone() || Widget == nullptr)
	{
		return nullptr;
	}
	const UDreamWidget* Root = Widget->GetRootWidgetInHierarchy() != nullptr ? Widget->GetRootWidgetInHierarchy() : Widget;
	if (Root->GetDisplayName() == InName.ToString())
	{
		return const_cast<UDreamWidget*>(Root);
	}
	return Root->FindChildByDisplayName(InName.ToString(), true);
}

UDreamWidget* UDreamNiagaraTrigger::ResolveFlightTarget()const
{
	return FlightTarget != nullptr ? FlightTarget.Get() : FindNamed(FlightTargetName);
}

UDreamWidget* UDreamNiagaraTrigger::ResolveProgressWidget()const
{
	return ProgressWidget != nullptr ? ProgressWidget.Get() : FindNamed(ProgressWidgetName);
}

FVector2D UDreamNiagaraTrigger::ToSystemLocal(const FVector& InWorldPoint)const
{
	const UDreamNiagaraSystem* System = FindSystem();
	const UDreamWidget* Widget = System != nullptr ? System->GetWidget() : nullptr;
	if (Widget == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	const FVector Local = Widget->GetWorldTransform().InverseTransformPosition(InWorldPoint);
	return FVector2D(Local.Y, Local.Z);
}

bool UDreamNiagaraTrigger::ReadProgress(float& OutPercent)const
{
	UDreamWidget* Widget = ResolveProgressWidget();
	const FFloatProperty* Property = Widget != nullptr ? FindFProperty<FFloatProperty>(Widget->GetClass(), DreamNiagaraTriggerLocal::PercentName) : nullptr;
	if (Property == nullptr)
	{
		return false;
	}
	OutPercent = Property->GetPropertyValue_InContainer(Widget);
	return true;
}

void UDreamNiagaraTrigger::WriteProgress(float InPercent)const
{
	UDreamWidget* Widget = ResolveProgressWidget();
	if (Widget == nullptr)
	{
		return;
	}
	// Through its setter, so that the widget draws the change.
	if (UFunction* Setter = Widget->FindFunction(TEXT("SetPercent")))
	{
		struct FArgs { float InPercent; } Args{ InPercent };
		Widget->ProcessEvent(Setter, &Args);
	}
}

void UDreamNiagaraTrigger::Fire(EDreamNiagaraTriggerAction InAction)
{
	UDreamNiagaraSystem* System = FindSystem();
	if (System == nullptr)
	{
		return;
	}
	const FVector2D Centre = System->GetWidget() != nullptr ? System->GetWidget()->GetLocalSpaceCenter() : FVector2D::ZeroVector;
	const FVector2D Pointer = bHasPointer ? LastPointerLocal : Centre;
	switch (InAction)
	{
	case EDreamNiagaraTriggerAction::BurstAtPointer:
		System->BurstAt(Pointer);
		break;
	case EDreamNiagaraTriggerAction::BurstAtCentre:
		System->BurstAt(Centre);
		break;
	case EDreamNiagaraTriggerAction::BurstToTarget:
		System->BurstTo(ResolveFlightTarget(), Pointer);
		break;
	case EDreamNiagaraTriggerAction::Activate:
		System->Activate(false);
		break;
	case EDreamNiagaraTriggerAction::Deactivate:
		System->Deactivate();
		break;
	case EDreamNiagaraTriggerAction::Restart:
		System->Activate(true);
		break;
	default:
		break;
	}
}

bool UDreamNiagaraTrigger::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData != nullptr)
	{
		LastPointerLocal = ToSystemLocal(EventData->WorldPoint);
		bHasPointer = true;
	}
	Fire(OnClick);
	float Percent = 0.0f;
	if (ClickAddsProgress > 0.0f && ReadProgress(Percent))
	{
		WriteProgress(Percent >= 1.0f ? 0.0f : FMath::Min(1.0f, Percent + ClickAddsProgress));
	}
	return true;
}

bool UDreamNiagaraTrigger::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData != nullptr)
	{
		LastPointerLocal = ToSystemLocal(EventData->WorldPoint);
		bHasPointer = true;
	}
	Fire(OnEnter);
	return true;
}

bool UDreamNiagaraTrigger::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	Fire(OnExit);
	return true;
}

void UDreamNiagaraTrigger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	float Percent = 0.0f;
	if (!ReadProgress(Percent))
	{
		return;
	}
	// On reaching full, once, and again only after it has dropped below.
	const bool bFull = Percent >= 1.0f - UE_KINDA_SMALL_NUMBER;
	if (bFull && !bProgressWasFull)
	{
		Fire(OnProgressFull);
	}
	bProgressWasFull = bFull;
}
