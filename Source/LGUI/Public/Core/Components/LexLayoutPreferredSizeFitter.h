// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutPreferredSizeFitter.generated.h"

class ULexWidget;

UCLASS(BlueprintType, DisplayName="Preferred Size Fitter")
class LGUI_API ULexLayoutPreferredSizeFitter : public ULexLayout
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter="GetFitWidth", Setter="SetFitWidth", meta = (AllowPrivateAccess = true))
	bool bFitWidth = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition="bFitWidth"))
	float AdditionalWidth = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter="GetFitHeight", Setter="SetFitWidth", meta = (AllowPrivateAccess = true))
	bool bFitHeight = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition="bFitHeight"))
	float AdditionalHeight = 0;

	void UpdateSize();
	virtual void OnUpdateLayout() override;
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) override;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual float GetPreferredWidth()const override;
	virtual float GetPreferredHeight()const override;

	UFUNCTION(BlueprintCallable, Category = "Layout")
	bool GetFitWidth()const{return bFitWidth;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	float GetAdditionalWidth()const{return AdditionalWidth;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	bool GetFitHeight()const{return bFitHeight;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	float GetAdditionalHeight()const{return AdditionalHeight;}

	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetFitWidth(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetAdditionalWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetFitHeight(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetAdditionalHeight(float Value);
};
