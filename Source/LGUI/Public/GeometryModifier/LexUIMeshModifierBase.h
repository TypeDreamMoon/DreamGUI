// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/LexUIGeometry.h"
#include "LGUI/Public/Core/Components/LexVisualBatchMesh.h"
#include "Components/ActorComponent.h"
#include "LexUIMeshModifierBase.generated.h"

class ULexVisualBatchMesh;
class ULexText;

UENUM(BlueprintType)
enum class ELexUIMeshModifierHelper_UITextModifyPositionType:uint8
{
	//Relative to character's origin position
	Relative,
	//Direct set character's position, relative to UIText's pivot position
	Absolute,
};
/** a helper class for UIGeometryModifierBase to easily modify ui geometry */
UCLASS(BlueprintType)
class LGUI_API ULexUIMeshModifierHelper : public ULexUIGeometryHelper
{
	GENERATED_BODY()
public:
	/** Get character's center position in UIText's rect range, and convert to 0-1 range (left is 0 and right is 1) */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float UITextHelperFunction_GetCharHorizontalPositionRatio01(ULexText* InUIText, int InCharIndex)const;
	/**
	 * Modify character's position & rotation & scale
	 * @param	InPositionType		Set position type, relative to origin position or absolute position
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void UITextHelperFunction_ModifyCharGeometry_Transform(ULexText* InUIText, int InCharIndex
			, ELexUIMeshModifierHelper_UITextModifyPositionType InPositionType
			, const FVector& InPosition
			, const FRotator& InRotator = FRotator::ZeroRotator
			, const FVector& InScale = FVector(1,1,1)
		);
	/**
	 * Get character's pivot position relative to UIText's pivot position
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void UITextHelperFunction_GetCharGeometry_AbsolutePosition(ULexText* InUIText, int InCharIndex, FVector& OutPosition)const;
	/**
	 * Modify character's position
	 * @param	InPositionType		Set position type, relative to origin position or absolute position
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void UITextHelperFunction_ModifyCharGeometry_Position(ULexText* InUIText, int InCharIndex
			, const FVector& InPosition, ELexUIMeshModifierHelper_UITextModifyPositionType InPositionType
		);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void UITextHelperFunction_ModifyCharGeometry_Rotate(ULexText* InUIText, int InCharIndex, const FRotator& InRotator = FRotator::ZeroRotator);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void UITextHelperFunction_ModifyCharGeometry_Scale(ULexText* InUIText, int InCharIndex, const FVector& InScale = FVector(1, 1, 1));
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void UITextHelperFunction_ModifyCharGeometry_Color(ULexText* InUIText, int InCharIndex, const FColor& InColor);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void UITextHelperFunction_ModifyCharGeometry_Alpha(ULexText* InUIText, int InCharIndex, const float& InAlpha);
};

/** 
 * For modify ui geometry, act like a filter.
 * Need UIBatchMeshRenderable component.
 */
UCLASS(Abstract, BlueprintType, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexUIMeshModifierBase : public UObject
{
	GENERATED_BODY()

public:	
	ULexUIMeshModifierBase();

protected:
	friend class ULexVisualBatchMesh;
	virtual void BeginPlay(){}
	virtual void EndPlay(){}
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Enable this geometry modifier */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bEnable = true;

private:
	mutable TWeakObjectPtr<ULexVisualBatchMesh> CacheLexVisual;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexVisualBatchMesh* GetLexVisual()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool GetEnable()const { return bEnable; }
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetEnable(bool value);
	/**
	 * Modify UI geometry's vertex and triangle.
	 * @param	InTriangleChanged		triangle changed
	 * @param	InUVChanged			vertex uv changed
	 * @param	InColorChanged			vertex color changed
	 * @param	InVertexPositionChanged			vertex position changed
	 */
	virtual void ModifyUIGeometry(
		FLexUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	);
	/**
	 * Will this modifier affect these geometry data? Save some calculation if not affect.
	 * For blueprint just make all to true, for easier use.
	 */
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
	{
		OutTriangleIndices = true;
		OutVertexPosition = true;
		OutUV = true;
		OutColor = true;
	}
protected:
	UPROPERTY(Transient) TObjectPtr<ULexUIMeshModifierHelper> GeometryModifierHelper = nullptr;
	/**
	 * Modify UI geometry's vertex and triangle.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI", meta = (DisplayName = "ModifyUIGeometry"))
		void ReceiveModifyUIGeometry(ULexUIMeshModifierHelper* InGeometryModifierHelper);
};
