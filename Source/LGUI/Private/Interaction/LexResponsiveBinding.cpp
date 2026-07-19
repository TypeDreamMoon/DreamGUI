// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Interaction/LexResponsiveBinding.h"
#include "UObject/UnrealType.h"

void ULexDataBinding::Awake()
{
	Super::Awake();
	if (UpdateMode != ELexBindingUpdateMode::Manual)
	{
		ApplyBinding();
	}
	if (UpdateMode != ELexBindingUpdateMode::EveryFrame)
	{
		SetCanExecuteTick(false);
	}
}

void ULexDataBinding::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ApplyBinding();
}

bool ULexDataBinding::ApplyBinding()
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
	if (ULexWidget* TargetWidget = Cast<ULexWidget>(Target))
	{
		if (TargetProperty == ULexWidget::GetPropertyName_Visibility())
		{
			TargetWidget->SetVisibility(*static_cast<const ELexWidgetVisibility*>(SourceValue));
			return true;
		}
		if (TargetProperty == ULexWidget::GetPropertyName_WidgetActive())
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
			TargetWidget->SetInteractable(*static_cast<const ELexWidgetInteractableType*>(SourceValue));
			return true;
		}
		if (TargetProperty == TEXT("Raycastable"))
		{
			TargetWidget->SetRaycastable(*static_cast<const ELexWidgetRaycastableType*>(SourceValue));
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

bool FLexResponsiveRule::Matches(const FVector2D& Size) const
{
	return Size.X >= MinWidth && Size.Y >= MinHeight
		&& (MaxWidth <= 0.0f || Size.X <= MaxWidth)
		&& (MaxHeight <= 0.0f || Size.Y <= MaxHeight);
}

void ULexResponsiveBehaviour::OnRegister()
{
	Super::OnRegister();
	BindParentDimensionEvent();
}

void ULexResponsiveBehaviour::OnUnregister()
{
	if (BoundParent.IsValid() && ParentDimensionHandle.IsValid())
	{
		BoundParent->GetDimensionChangedEvent().Remove(ParentDimensionHandle);
	}
	ParentDimensionHandle.Reset();
	BoundParent = nullptr;
	Super::OnUnregister();
}

void ULexResponsiveBehaviour::BindParentDimensionEvent()
{
	if (BoundParent.IsValid() && ParentDimensionHandle.IsValid())
	{
		BoundParent->GetDimensionChangedEvent().Remove(ParentDimensionHandle);
	}
	ParentDimensionHandle.Reset();
	BoundParent = GetWidget() ? GetWidget()->GetParent() : nullptr;
	if (BoundParent.IsValid())
	{
		ParentDimensionHandle = BoundParent->GetDimensionChangedEvent().AddUObject(this, &ULexResponsiveBehaviour::HandleParentDimensionChanged);
	}
}

void ULexResponsiveBehaviour::HandleParentDimensionChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	if (WidthChanged || HeightChanged)
	{
		EvaluateResponsiveRules();
	}
}

void ULexResponsiveBehaviour::Awake()
{
	Super::Awake();
	EvaluateResponsiveRules();
}

void ULexResponsiveBehaviour::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	if (WidthChanged || HeightChanged)
	{
		EvaluateResponsiveRules();
	}
}

void ULexResponsiveBehaviour::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();
	BindParentDimensionEvent();
	EvaluateResponsiveRules();
}

void ULexResponsiveBehaviour::EvaluateResponsiveRules()
{
	ULexWidget* Widget = GetWidget();
	if (!Widget)
	{
		return;
	}
	const FVector2D ReferenceSize = Widget->GetParent() ? Widget->GetParent()->GetSize() : Widget->GetSize();
	for (const FLexResponsiveRule& Rule : Rules)
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
