// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexPanelSlot.h"
#include "LexPanelLayouts.generated.h"

UENUM(BlueprintType)
enum class ELexPanelOrientation : uint8
{
	Horizontal,
	Vertical,
};

UENUM(BlueprintType)
enum class ELexScaleBoxStretch : uint8
{
	None,
	Fill,
	ScaleToFit,
	ScaleToFill,
	ScaleToFitX,
	ScaleToFitY,
	UserSpecified,
};

UCLASS(Abstract, BlueprintType)
class LGUI_API ULexPanelLayoutBase : public ULexLayoutContainer
{
	GENERATED_BODY()

protected:
	FVector2f PreferredSize = FVector2f::ZeroVector;
	ULexPanelSlot* EnsureSlot(ULexWidget* Child) const;
	TArray<ULexWidget*> CollectLayoutChildren() const;
	FVector2D GetDesiredSize(ULexWidget* Child) const;
	void ApplyChildRect(ULexWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill = false) const;
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) const override;

public:
	virtual FVector2f GetLayoutPreferredSize() override { return PreferredSize; }
	UFUNCTION(BlueprintCallable, Category = "Panel")
	void RequestLayoutRefresh();
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-CanvasPanel")
class LGUI_API ULexLayoutContainerCanvasPanel : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CanvasPanel")
	bool bSortChildrenByZOrder = true;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-Overlay")
class LGUI_API ULexLayoutContainerOverlay : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FMargin Padding;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-StackBox")
class LGUI_API ULexLayoutContainerStackBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StackBox")
	ELexPanelOrientation Orientation = ELexPanelOrientation::Vertical;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StackBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StackBox", meta = (ClampMin = "0.0"))
	float Spacing = 0.0f;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-HorizontalBox")
class LGUI_API ULexLayoutContainerHorizontalBox : public ULexLayoutContainerStackBox
{
	GENERATED_BODY()
public:
	ULexLayoutContainerHorizontalBox();
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-VerticalBox")
class LGUI_API ULexLayoutContainerVerticalBox : public ULexLayoutContainerStackBox
{
	GENERATED_BODY()
public:
	ULexLayoutContainerVerticalBox();
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-WrapBox")
class LGUI_API ULexLayoutContainerWrapBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WrapBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WrapBox")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WrapBox", meta = (ClampMin = "0.0"))
	float WrapSize = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WrapBox")
	bool bExplicitWrapSize = false;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-GridPanel")
class LGUI_API ULexLayoutContainerGridPanel : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridPanel")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridPanel")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridPanel")
	TArray<float> ColumnFill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridPanel")
	TArray<float> RowFill;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-UniformGridPanel")
class LGUI_API ULexLayoutContainerUniformGridPanel : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UniformGridPanel")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UniformGridPanel")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotHeight = 0.0f;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-SizeBox")
class LGUI_API ULexLayoutContainerSizeBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SizeBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SizeBox")
	bool bOverrideWidth = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SizeBox", meta = (EditCondition = "bOverrideWidth", ClampMin = "0.0"))
	float WidthOverride = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SizeBox")
	bool bOverrideHeight = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SizeBox", meta = (EditCondition = "bOverrideHeight", ClampMin = "0.0"))
	float HeightOverride = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SizeBox", meta = (ClampMin = "0.0"))
	FVector2D MinDesiredSize = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SizeBox", meta = (ClampMin = "0.0"))
	FVector2D MaxDesiredSize = FVector2D::ZeroVector;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-ScaleBox")
class LGUI_API ULexLayoutContainerScaleBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScaleBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScaleBox")
	ELexScaleBoxStretch Stretch = ELexScaleBoxStretch::ScaleToFit;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScaleBox", meta = (ClampMin = "0.0"))
	float UserSpecifiedScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScaleBox")
	bool bIgnoreInheritedScale = false;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-SafeZone")
class LGUI_API ULexLayoutContainerSafeZone : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SafeZone")
	bool bUsePlatformSafeZone = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SafeZone")
	bool bPadLeft = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SafeZone")
	bool bPadTop = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SafeZone")
	bool bPadRight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SafeZone")
	bool bPadBottom = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SafeZone")
	FMargin SafePadding;
	/** Per-side fraction of this widget's size, useful for device profiles and previewing notches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SafeZone", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	FMargin NormalizedSafePadding;
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-ScrollBox")
class LGUI_API ULexLayoutContainerScrollBox : public ULexLayoutContainerStackBox
{
	GENERATED_BODY()
public:
	ULexLayoutContainerScrollBox();
	virtual void OnRegister() override;
};

UCLASS(BlueprintType, DisplayName = "LayoutContainer-WidgetSwitcher")
class LGUI_API ULexLayoutContainerWidgetSwitcher : public ULexPanelLayoutBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WidgetSwitcher", meta = (AllowPrivateAccess = true, ClampMin = "0"))
	int32 ActiveWidgetIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSwitcher")
	FMargin Padding;
	virtual void CalculateLayout() override;
	UFUNCTION(BlueprintCallable, Category = "WidgetSwitcher")
	void SetActiveWidgetIndex(int32 Value);
	UFUNCTION(BlueprintPure, Category = "WidgetSwitcher")
	ULexWidget* GetActiveWidget()const;
};
