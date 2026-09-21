// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamResponsiveBinding.generated.h"

UENUM(BlueprintType)
enum class EDreamBindingUpdateMode : uint8
{
	OnAwake,
	EveryFrame,
	Manual,
};

/** Reflection-based one-way binding for compatible UPROPERTY values. */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamDataBinding : public UDreamUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	TObjectPtr<UObject> SourceObject = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FName SourceProperty;
	/** Defaults to the owning DreamWidget when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	TObjectPtr<UObject> TargetObject = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FName TargetProperty;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	EDreamBindingUpdateMode UpdateMode = EDreamBindingUpdateMode::OnAwake;
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Binding")
	bool ApplyBinding();
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamResponsiveRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive")
	FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive", meta = (ClampMin = "0.0"))
	float MinWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive", meta = (ClampMin = "0.0"))
	float MaxWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive", meta = (ClampMin = "0.0"))
	float MinHeight = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive", meta = (ClampMin = "0.0"))
	float MaxHeight = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive")
	EDreamWidgetVisibility Visibility = EDreamWidgetVisibility::Visible;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RenderOpacity = 1.0f;

	bool Matches(const FVector2D& Size) const;
};

/**
 * Applies the first matching breakpoint using the parent size (or self size at the root).
 *
 * The rules are OVERRIDES on the appearance the widget was authored with, not a complete
 * description of it: a size that matches no rule puts the widget back the way it was found rather
 * than leaving it wearing the last rule's costume. See EvaluateResponsiveRules for why that is the
 * chosen answer and what the baseline it restores actually is.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamResponsiveBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive")
	TArray<FDreamResponsiveRule> Rules;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Responsive")
	FName ActiveRule;
	FDelegateHandle ParentDimensionHandle;
	TWeakObjectPtr<UDreamWidget> BoundParent;
	/**
	 * The widget's own appearance from the moment before a rule first overrode it, held for exactly
	 * as long as a rule is active. Deliberately not a UPROPERTY and not authored state: it is a
	 * record of what this component borrowed, and it is meaningless across a load because ActiveRule
	 * is Transient too, so nothing is owed back until this component takes something again.
	 */
	bool bHasBaselineAppearance = false;
	EDreamWidgetVisibility BaselineVisibility = EDreamWidgetVisibility::Visible;
	float BaselineRenderOpacity = 1.0f;
	void CaptureBaselineAppearance(UDreamWidget* Widget);
	void RestoreBaselineAppearance(UDreamWidget* Widget);
	void BindParentDimensionEvent();
	void HandleParentDimensionChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged);
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Awake() override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged) override;
	virtual void OnAttachmentChanged() override;

public:
	UFUNCTION(BlueprintCallable, Category = "Responsive")
	void EvaluateResponsiveRules();
	UFUNCTION(BlueprintPure, Category = "Responsive")
	FName GetActiveRule()const { return ActiveRule; }
};
