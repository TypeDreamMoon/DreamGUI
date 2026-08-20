// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamResponsiveBinding.h"
#include "UObject/UnrealType.h"

void UDreamDataBinding::Awake()
{
	Super::Awake();
	if (UpdateMode != EDreamBindingUpdateMode::Manual)
	{
		ApplyBinding();
	}
	if (UpdateMode != EDreamBindingUpdateMode::EveryFrame)
	{
		SetCanExecuteTick(false);
	}
}

void UDreamDataBinding::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ApplyBinding();
}

bool UDreamDataBinding::ApplyBinding()
{
	UObject* Source = SourceObject;
	UObject* Target = TargetObject ? TargetObject.Get() : GetWidget();
	if (!IsValid(Source) || !IsValid(Target) || SourceProperty.IsNone() || TargetProperty.IsNone())
	{
		return false;
	}
	FProperty* SourceValueProperty = Source->GetClass()->FindPropertyByName(SourceProperty);
	FProperty* TargetValueProperty = Target->GetClass()->FindPropertyByName(TargetProperty);
	if (!SourceValueProperty || !TargetValueProperty || !SourceValueProperty->SameType(TargetValueProperty))
	{
		return false;
	}
	const void* SourceValue = SourceValueProperty->ContainerPtrToValuePtr<void>(Source);
	void* TargetValue = TargetValueProperty->ContainerPtrToValuePtr<void>(Target);
	if (UDreamWidget* TargetWidget = Cast<UDreamWidget>(Target))
	{
		if (TargetProperty == UDreamWidget::GetPropertyName_Visibility())
		{
			TargetWidget->SetVisibility(*static_cast<const EDreamWidgetVisibility*>(SourceValue));
			return true;
		}
		if (TargetProperty == UDreamWidget::GetPropertyName_WidgetActive())
		{
			const FBoolProperty* BoolProperty = CastFieldChecked<FBoolProperty>(SourceValueProperty);
			TargetWidget->SetWidgetActive(BoolProperty->GetPropertyValue(SourceValue));
			return true;
		}
		if (TargetProperty == TEXT("RenderOpacity"))
		{
			TargetWidget->SetRenderOpacity(*static_cast<const float*>(SourceValue));
			return true;
		}
		if (TargetProperty == TEXT("Interactable"))
		{
			TargetWidget->SetInteractable(*static_cast<const EDreamWidgetInteractableType*>(SourceValue));
			return true;
		}
		if (TargetProperty == TEXT("Raycastable"))
		{
			TargetWidget->SetRaycastable(*static_cast<const EDreamWidgetRaycastableType*>(SourceValue));
			return true;
		}
	}
	TargetValueProperty->CopyCompleteValue(TargetValue, SourceValue);
#if WITH_EDITOR
	FPropertyChangedEvent ChangeEvent(TargetValueProperty);
	Target->PostEditChangeProperty(ChangeEvent);
#endif
	return true;
}

bool FDreamResponsiveRule::Matches(const FVector2D& Size) const
{
	return Size.X >= MinWidth && Size.Y >= MinHeight
		&& (MaxWidth <= 0.0f || Size.X <= MaxWidth)
		&& (MaxHeight <= 0.0f || Size.Y <= MaxHeight);
}

void UDreamResponsiveBehaviour::OnRegister()
{
	Super::OnRegister();
	BindParentDimensionEvent();
}

void UDreamResponsiveBehaviour::OnUnregister()
{
	if (BoundParent.IsValid() && ParentDimensionHandle.IsValid())
	{
		BoundParent->GetDimensionChangedEvent().Remove(ParentDimensionHandle);
	}
	ParentDimensionHandle.Reset();
	BoundParent = nullptr;
	Super::OnUnregister();
}

void UDreamResponsiveBehaviour::BindParentDimensionEvent()
{
	if (BoundParent.IsValid() && ParentDimensionHandle.IsValid())
	{
		BoundParent->GetDimensionChangedEvent().Remove(ParentDimensionHandle);
	}
	ParentDimensionHandle.Reset();
	BoundParent = GetWidget() ? GetWidget()->GetParent() : nullptr;
	if (BoundParent.IsValid())
	{
		ParentDimensionHandle = BoundParent->GetDimensionChangedEvent().AddUObject(this, &UDreamResponsiveBehaviour::HandleParentDimensionChanged);
	}
}

void UDreamResponsiveBehaviour::HandleParentDimensionChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	if (WidthChanged || HeightChanged)
	{
		EvaluateResponsiveRules();
	}
}

void UDreamResponsiveBehaviour::Awake()
{
	Super::Awake();
	EvaluateResponsiveRules();
}

void UDreamResponsiveBehaviour::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	if (WidthChanged || HeightChanged)
	{
		EvaluateResponsiveRules();
	}
}

void UDreamResponsiveBehaviour::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();
	BindParentDimensionEvent();
	EvaluateResponsiveRules();
}

void UDreamResponsiveBehaviour::EvaluateResponsiveRules()
{
	UDreamWidget* Widget = GetWidget();
	if (!Widget)
	{
		return;
	}
	const FVector2D ReferenceSize = Widget->GetParent() ? Widget->GetParent()->GetSize() : Widget->GetSize();
	for (const FDreamResponsiveRule& Rule : Rules)
	{
		if (Rule.Matches(ReferenceSize))
		{
			ActiveRule = Rule.Name;
			Widget->SetVisibility(Rule.Visibility);
			Widget->SetRenderOpacity(Rule.RenderOpacity);
			return;
		}
	}
	ActiveRule = NAME_None;
}
