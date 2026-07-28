// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexWidget.h"
#include "LexResponsiveBinding.generated.h"

UENUM(BlueprintType)
enum class ELexBindingUpdateMode : uint8
{
	OnAwake,
	EveryFrame,
	Manual,
};

/** Reflection-based one-way binding for compatible UPROPERTY values. */
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexDataBinding : public ULexUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	TObjectPtr<UObject> SourceObject = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FName SourceProperty;
	/** Defaults to the owning LexWidget when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	TObjectPtr<UObject> TargetObject = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FName TargetProperty;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	ELexBindingUpdateMode UpdateMode = ELexBindingUpdateMode::OnAwake;
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Binding")
	bool ApplyBinding();
};

USTRUCT(BlueprintType)
struct LGUI_API FLexResponsiveRule
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
	ELexWidgetVisibility Visibility = ELexWidgetVisibility::Visible;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RenderOpacity = 1.0f;

	bool Matches(const FVector2D& Size) const;
};

/** Applies the first matching breakpoint using the parent size (or self size at the root). */
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexResponsiveBehaviour : public ULexUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Responsive")
	TArray<FLexResponsiveRule> Rules;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Responsive")
	FName ActiveRule;
	FDelegateHandle ParentDimensionHandle;
	TWeakObjectPtr<ULexWidget> BoundParent;
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
