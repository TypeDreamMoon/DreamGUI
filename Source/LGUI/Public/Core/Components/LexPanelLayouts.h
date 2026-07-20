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
	virtual void OnUnregister() override;
	ULexPanelSlot* EnsureSlot(ULexWidget* Child) const;
	const ULexPanelSlot* GetSlot(const ULexWidget* Child) const;
	TArray<ULexWidget*> CollectLayoutChildren(bool bEnsureSlots = true) const;
	FVector2D GetDesiredSize(ULexWidget* Child) const;
	void ApplyChildRect(ULexWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill = false) const;
	bool BeginLayoutPass();
	virtual FVector2f MeasureLayout() const;
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

public:
	virtual FVector2f GetLayoutPreferredSize() override;
	UFUNCTION(BlueprintCallable, Category = "Panel")
	void RequestLayoutRefresh();
};

UCLASS(BlueprintType, DisplayName = "Canvas Panel")
class LGUI_API ULexLayoutContainerCanvasPanel : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSortChildrenByZOrder, Category = "CanvasPanel")
	bool bSortChildrenByZOrder = true;
	UFUNCTION(BlueprintSetter) void SetSortChildrenByZOrder(bool Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Overlay")
class LGUI_API ULexLayoutContainerOverlay : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "Overlay")
	FMargin Padding;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Stack Box")
class LGUI_API ULexLayoutContainerStackBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOrientation, Category = "StackBox")
	ELexPanelOrientation Orientation = ELexPanelOrientation::Vertical;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "StackBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "StackBox", meta = (ClampMin = "0.0"))
	float Spacing = 0.0f;
	UFUNCTION(BlueprintSetter) void SetOrientation(ELexPanelOrientation Value);
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(float Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Horizontal Box")
class LGUI_API ULexLayoutContainerHorizontalBox : public ULexLayoutContainerStackBox
{
	GENERATED_BODY()
public:
	ULexLayoutContainerHorizontalBox();
};

UCLASS(BlueprintType, DisplayName = "Vertical Box")
class LGUI_API ULexLayoutContainerVerticalBox : public ULexLayoutContainerStackBox
{
	GENERATED_BODY()
public:
	ULexLayoutContainerVerticalBox();
};

UCLASS(BlueprintType, DisplayName = "Wrap Box")
class LGUI_API ULexLayoutContainerWrapBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "WrapBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "WrapBox")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetWrapSize, Category = "WrapBox", meta = (ClampMin = "0.0"))
	float WrapSize = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetExplicitWrapSize, Category = "WrapBox")
	bool bExplicitWrapSize = false;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetWrapSize(float Value);
	UFUNCTION(BlueprintSetter) void SetExplicitWrapSize(bool Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Grid Panel")
class LGUI_API ULexLayoutContainerGridPanel : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "GridPanel")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "GridPanel")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColumnFill, Category = "GridPanel")
	TArray<float> ColumnFill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRowFill, Category = "GridPanel")
	TArray<float> RowFill;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetColumnFill(const TArray<float>& Value);
	UFUNCTION(BlueprintSetter) void SetRowFill(const TArray<float>& Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Uniform Grid Panel")
class LGUI_API ULexLayoutContainerUniformGridPanel : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "UniformGridPanel")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSpacing, Category = "UniformGridPanel")
	FVector2D Spacing = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSlotWidth, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSlotHeight, Category = "UniformGridPanel", meta = (ClampMin = "0.0"))
	float MinDesiredSlotHeight = 0.0f;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetSpacing(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSlotWidth(float Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSlotHeight(float Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Size Box")
class LGUI_API ULexLayoutContainerSizeBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) const override;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "SizeBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOverrideWidth, Category = "SizeBox")
	bool bOverrideWidth = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetWidthOverride, Category = "SizeBox", meta = (EditCondition = "bOverrideWidth", ClampMin = "0.0"))
	float WidthOverride = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOverrideHeight, Category = "SizeBox")
	bool bOverrideHeight = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHeightOverride, Category = "SizeBox", meta = (EditCondition = "bOverrideHeight", ClampMin = "0.0"))
	float HeightOverride = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinDesiredSize, Category = "SizeBox", meta = (ClampMin = "0.0"))
	FVector2D MinDesiredSize = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMaxDesiredSize, Category = "SizeBox", meta = (ClampMin = "0.0"))
	FVector2D MaxDesiredSize = FVector2D::ZeroVector;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetOverrideWidth(bool Value);
	UFUNCTION(BlueprintSetter) void SetWidthOverride(float Value);
	UFUNCTION(BlueprintSetter) void SetOverrideHeight(bool Value);
	UFUNCTION(BlueprintSetter) void SetHeightOverride(float Value);
	UFUNCTION(BlueprintSetter) void SetMinDesiredSize(FVector2D Value);
	UFUNCTION(BlueprintSetter) void SetMaxDesiredSize(FVector2D Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Scale Box")
class LGUI_API ULexLayoutContainerScaleBox : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) const override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	void UpdateClippingOverride();
	TWeakObjectPtr<ULexWidget> ScaledChild;
	bool bAppliedDefaultClipping = false;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "ScaleBox")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStretch, Category = "ScaleBox")
	ELexScaleBoxStretch Stretch = ELexScaleBoxStretch::ScaleToFit;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUserSpecifiedScale, Category = "ScaleBox", meta = (ClampMin = "0.0"))
	float UserSpecifiedScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetIgnoreInheritedScale, Category = "ScaleBox")
	bool bIgnoreInheritedScale = false;
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetStretch(ELexScaleBoxStretch Value);
	UFUNCTION(BlueprintSetter) void SetUserSpecifiedScale(float Value);
	UFUNCTION(BlueprintSetter) void SetIgnoreInheritedScale(bool Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Safe Zone")
class LGUI_API ULexLayoutContainerSafeZone : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	FMargin GetCombinedSafePadding() const;
	virtual FVector2f MeasureLayout() const override;
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) const override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	void HandleSafeFrameChanged();
	FDelegateHandle SafeFrameChangedHandle;
public:
	virtual int32 GetMaxChildren() const override { return 1; }
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUsePlatformSafeZone, Category = "SafeZone")
	bool bUsePlatformSafeZone = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadLeft, Category = "SafeZone")
	bool bPadLeft = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadTop, Category = "SafeZone")
	bool bPadTop = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadRight, Category = "SafeZone")
	bool bPadRight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadBottom, Category = "SafeZone")
	bool bPadBottom = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSafePadding, Category = "SafeZone")
	FMargin SafePadding;
	/** Per-side fraction of this widget's size, useful for device profiles and previewing notches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNormalizedSafePadding, Category = "SafeZone", meta = (ClampMin = "0.0", ClampMax = "0.499"))
	FMargin NormalizedSafePadding;
	UFUNCTION(BlueprintSetter) void SetUsePlatformSafeZone(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadLeft(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadTop(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadRight(bool Value);
	UFUNCTION(BlueprintSetter) void SetPadBottom(bool Value);
	UFUNCTION(BlueprintSetter) void SetSafePadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetNormalizedSafePadding(FMargin Value);
	virtual void CalculateLayout() override;
};

UCLASS(BlueprintType, DisplayName = "Scroll Box Layout")
class LGUI_API ULexLayoutContainerScrollBox : public ULexLayoutContainerStackBox
{
	GENERATED_BODY()
protected:
	bool bAppliedDefaultClipping = false;
public:
	ULexLayoutContainerScrollBox();
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};

UCLASS(BlueprintType, DisplayName = "Widget Switcher")
class LGUI_API ULexLayoutContainerWidgetSwitcher : public ULexPanelLayoutBase
{
	GENERATED_BODY()
protected:
	virtual FVector2f MeasureLayout() const override;
	virtual void OnUnregister() override;
	TWeakObjectPtr<ULexWidget> ActiveWidget;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetActiveWidgetIndex, Category = "WidgetSwitcher", meta = (AllowPrivateAccess = true, ClampMin = "0"))
	int32 ActiveWidgetIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "WidgetSwitcher")
	FMargin Padding;
	virtual void CalculateLayout() override;
	UFUNCTION(BlueprintSetter, BlueprintCallable, Category = "WidgetSwitcher")
	void SetActiveWidgetIndex(int32 Value);
	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintPure, Category = "WidgetSwitcher")
	ULexWidget* GetActiveWidget()const;
};
